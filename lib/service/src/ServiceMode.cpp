#include "ServiceMode.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NetworkUdp.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CaptiveDns.h"
#include "ConfigEditor.h"
#include "Log.h"
#include "PersistentStore.h"
#include "SdCardFs.h"
#include "ServicePassword.h"
#include "StorageFormat.h"
#include "SystemInfo.h"
#include "Updater.h"
#include "Version.h"
#include "NoClientExit.h"
#include "WebUi.h"

namespace {

constexpr const char* kTag = "service";
constexpr size_t kResponseSize = 6144;  // per request, freed afterwards
constexpr uint8_t kMaxClients = 2;  // each WiFi station costs RAM
constexpr uint32_t kRebootDelayMs = 500;
constexpr uint32_t kLowHeapWarning = 30000;
// The SD card isn't used in service mode, except while a log file request runs: mounting it
// costs ~5-9 KB of heap (FAT structures, file buffer), which must not starve WiFi.
constexpr uint32_t kMinHeapForSd = 16000;
constexpr size_t kMaxListedLogs = 40;  // newest files only
constexpr size_t kDownloadChunk = 1436;  // one TCP segment

// Server objects live on the heap and are created in begin(): normal mode never pays for them.
// Response buffers are allocated per request (no PSRAM: nothing big stays allocated).
struct Context {
  ServiceMode::Options options;
  WebServer* server = nullptr;
  NetworkUDP* dns = nullptr;  // captive DNS, polled from the main loop (no extra task)
  char* response = nullptr;   // valid during one request
  char ssid[33] = {};
  uint32_t lastActivityMs = 0;
  uint8_t lastClients = 0;  // to notice a newly connected device
  NoClientExit noClientExit;
  uint32_t rebootAtMs = 0;
  bool rebootPending = false;
  updater::ImageUpload* upload = nullptr;  // only while a firmware upload runs
  uint32_t uploadStartMs = 0;
  uint32_t uploadLastChunkMs = 0;
  uint32_t uploadMaxGapMs = 0;
  size_t uploadLastLogBytes = 0;
  updater::RunningInfo running{};
};
Context g;

// Starts a request: marks activity (unless it is automatic, like the UI's status poll) and
// allocates the response buffer for this request.
// Returns the owner; g.response points into it until it goes out of scope.
std::unique_ptr<char[]> beginRequest(bool activity = true) {
  if (activity) g.lastActivityMs = millis();
  auto buf = sysinfo::makeTempBuffer(kResponseSize);
  g.response = buf.get();
  return buf;
}

bool outOfMemory(const std::unique_ptr<char[]>& buf) {
  if (buf) return false;
  g.server->send(503, "application/json", "{\"ok\":false,\"error\":\"out of memory\"}");
  return true;
}

void sendJson(int code, const char* json, size_t len) {
  g.server->sendHeader("Cache-Control", "no-store");
  g.server->setContentLength(len);
  g.server->send(code, "application/json", "");
  g.server->sendContent(json, len);
}

void sendResult(bool ok, const char* err) {
  JsonDocument doc;
  doc["ok"] = ok;
  if (!ok) doc["error"] = err;
  const size_t len = serializeJson(doc, g.response, kResponseSize);
  sendJson(ok ? 200 : 400, g.response, len);
}

// Parses the request body as JSON object. Sends an error and returns false if that fails.
bool readBody(JsonDocument& doc) {
  if (!g.server->hasArg("plain")) {
    sendResult(false, "request body missing");
    return false;
  }
  const String& body = g.server->arg("plain");
  if (deserializeJson(doc, body) != DeserializationError::Ok || !doc.is<JsonObject>()) {
    sendResult(false, "request body must be a JSON object");
    return false;
  }
  return true;
}

// ---- handlers ------------------------------------------------------------------------

void handleIndex() {
  g.lastActivityMs = millis();
  g.server->sendHeader("Cache-Control", "no-store");
  g.server->send_P(200, "text/html; charset=utf-8", webui::indexHtml(), webui::indexHtmlLen());
}

void handleStatus() {
  // Polled by an open page every 30 s: does not count as activity, or a forgotten tab would keep
  // service mode open forever.
  auto buf = beginRequest(false);
  if (outOfMemory(buf)) return;
  const uint32_t now = millis();
  ServiceMode mode;
  int n = snprintf(g.response, kResponseSize,
                   "{\"mode\":\"service\",\"firmware\":\"%s\",\"service\":{\"ssid\":\"%s\",\"clients\":%u,"
                   "\"uptimeS\":%lu,\"secondsLeft\":%lu,\"apTimeoutMin\":%u,\"noClientExitS\":%lu,"
                   "\"defaultPassword\":%s,\"freeHeap\":%lu,\"minFreeHeap\":%lu},"
                   "\"slot\":\"%s\",\"lastUpdateRolledBack\":%s,\"snapshot\":",
                   SBH_FW_VERSION, g.ssid, mode.clients(), static_cast<unsigned long>(sysinfo::uptimeMs() / 1000),
                   static_cast<unsigned long>(mode.secondsLeft(now)), g.options.apTimeoutMin,
                   static_cast<unsigned long>(g.noClientExit.secondsLeft(mode.clients(), now)),
                   servicepw::isSet() ? "false" : "true",
                   static_cast<unsigned long>(sysinfo::freeHeap()), static_cast<unsigned long>(sysinfo::minFreeHeap()),
                   g.running.partition, g.running.previousRolledBack ? "true" : "false");
  size_t len = static_cast<size_t>(n);
  size_t snapLen = 0;
  if (g.options.snapshot != nullptr) {
    snapLen = snapshot::toJson(*g.options.snapshot, g.response + len, kResponseSize - len - 2, false);
  }
  if (snapLen == 0) {
    memcpy(g.response + len, "null", 4);
    snapLen = 4;
  }
  len += snapLen;
  g.response[len++] = '}';
  g.response[len] = '\0';
  sendJson(200, g.response, len);
}

void handleLog() {
  auto buf = beginRequest();
  if (outOfMemory(buf)) return;
  JsonDocument doc;
  JsonArray before = doc["before"].to<JsonArray>();
  if (g.options.snapshot != nullptr) {
    const StatusSnapshot& s = *g.options.snapshot;
    for (uint8_t i = 0; i < s.logCount; ++i) {
      JsonObject o = before.add<JsonObject>();
      o["uptimeS"] = s.log[i].uptimeS;
      char level[2] = {s.log[i].level, '\0'};
      o["level"] = level;
      o["tag"] = s.log[i].tag;
      o["msg"] = s.log[i].msg;
    }
  }
  if (measureJson(doc) + 1 > kResponseSize) {
    sendResult(false, "log too large");
    return;
  }
  const size_t len = serializeJson(doc, g.response, kResponseSize);
  sendJson(200, g.response, len);
}

void handleGetConfig() {
  auto buf = beginRequest();
  if (outOfMemory(buf)) return;
  char err[cfgedit::kErrLen];
  bool differs = false;
  int n = snprintf(g.response, kResponseSize, "{\"appKeySet\":%s,\"config\":",
                   persist::hasSecret(persist::secret::kLoRaAppKey) ? "true" : "false");
  size_t len = static_cast<size_t>(n);
  const size_t cfgLen =
      cfgedit::storedJson(*g.options.running, g.response + len, kResponseSize - len - 40, false, differs, err, sizeof(err));
  if (cfgLen == 0) {
    sendResult(false, err);
    return;
  }
  len += cfgLen;
  n = snprintf(g.response + len, kResponseSize - len, ",\"differsFromRunning\":%s}", differs ? "true" : "false");
  len += static_cast<size_t>(n);
  sendJson(200, g.response, len);
}

void handlePostConfig() {
  auto buf = beginRequest();
  if (outOfMemory(buf)) return;
  if (!g.server->hasArg("plain")) {
    sendResult(false, "request body missing");
    return;
  }
  const String& body = g.server->arg("plain");
  char err[cfgedit::kErrLen];
  const bool ok = cfgedit::storeJson(body.c_str(), body.length(), err, sizeof(err));
  sendResult(ok, err);
}

void handlePostField() {
  auto buf = beginRequest();
  if (outOfMemory(buf)) return;
  JsonDocument doc;
  if (!readBody(doc)) return;
  const char* field = doc["field"] | "";
  const char* value = doc["value"] | "";
  // "uplinks.devEui" -> group "uplinks", key "devEui"; "name" -> no group
  char group[24] = {};
  const char* key = field;
  const char* dot = strchr(field, '.');
  if (dot != nullptr && static_cast<size_t>(dot - field) < sizeof(group)) {
    memcpy(group, field, static_cast<size_t>(dot - field));
    key = dot + 1;
  }
  char err[cfgedit::kErrLen];
  const bool ok = cfgedit::setField(group[0] != '\0' ? group : nullptr, key, value, *g.options.running, err, sizeof(err));
  sendResult(ok, err);
}

void handlePostAppKey() {
  auto buf = beginRequest();
  if (outOfMemory(buf)) return;
  JsonDocument doc;
  if (!readBody(doc)) return;
  char err[cfgedit::kErrLen];
  const bool ok = cfgedit::setAppKeyHex(doc["appKey"] | "", err, sizeof(err));
  sendResult(ok, err);
}

void handlePostServicePassword() {
  auto buf = beginRequest();
  if (outOfMemory(buf)) return;
  JsonDocument doc;
  if (!readBody(doc)) return;
  char err[cfgedit::kErrLen];
  const bool ok = servicepw::set(doc["password"] | "", err, sizeof(err));
  if (ok) LOGI(kTag, "service-mode password changed from the web UI");
  sendResult(ok, err);
}

void handleReboot() {
  auto buf = beginRequest();
  if (outOfMemory(buf)) return;
  sendResult(true, "");
  LOGI(kTag, "reboot requested from the web UI");
  g.rebootPending = true;
  g.rebootAtMs = millis() + kRebootDelayMs;  // let the response go out first
}

// The captive DNS is closed during a firmware upload: nobody answers it then (the upload blocks
// the main loop), and queued queries would hold WiFi buffers.
void pauseDns(bool pause) {
  if (g.dns == nullptr) return;
  if (pause) {
    g.dns->stop();
  } else {
    g.dns->begin(53);
  }
}

// --- log files on the SD card ---------------------------------------------------------

// Error answer without the per-request response buffer: the SD handlers leave the heap to the
// card (sendResult() needs beginRequest()'s buffer).
void sendSmallError(const char* msg) {
  char json[160];
  const int n = snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", msg);
  sendJson(400, json, static_cast<size_t>(n));
}

// Mounts the card for the lifetime of one request.
class SdSession {
 public:
  SdSession() : ok_(sysinfo::freeHeap() >= kMinHeapForSd && fs_.mount()) {}
  ~SdSession() { fs_.unmount(); }
  bool ok() const { return ok_; }
  SdCardFs& fs() { return fs_; }

 private:
  SdCardFs fs_;
  bool ok_;
};

struct LogEntry {
  uint64_t key;
  char name[24];
};

// Keeps the newest kMaxListedLogs log files (by boot number and part) older than `before`: one
// page of the list. The next page starts before the oldest file of this one.
struct LogScan {
  LogEntry items[kMaxListedLogs];
  size_t count = 0;
  uint32_t total = 0;
  uint32_t older = 0;  // files older than `before`, this page included
  uint64_t before = UINT64_MAX;
};

void onLogFile(const char* name, void* p) {
  LogScan& s = *static_cast<LogScan*>(p);
  uint32_t boot;
  uint16_t part;
  const size_t len = strlen(name);
  if (!storagefmt::parseFileName(name, boot, part) || len >= sizeof(LogEntry::name) || len < 4 ||
      strcmp(name + len - 4, ".log") != 0) {
    return;
  }
  ++s.total;
  const uint64_t key = storagefmt::fileKey(boot, part);
  if (key >= s.before) return;
  ++s.older;
  size_t slot = s.count;
  if (s.count == kMaxListedLogs) {  // full: replace the oldest kept entry if this one is newer
    slot = 0;
    for (size_t i = 1; i < s.count; ++i) {
      if (s.items[i].key < s.items[slot].key) slot = i;
    }
    if (s.items[slot].key >= key) return;
  } else {
    ++s.count;
  }
  s.items[slot].key = key;
  strcpy(s.items[slot].name, name);
}

void handleSdLogList() {
  g.lastActivityMs = millis();
  sysinfo::watchdogFeed();
  auto scan = sysinfo::makeTemp<LogScan>();
  if (!scan) {
    sendSmallError("out of memory");
    return;
  }
  if (g.server->hasArg("before")) {
    uint32_t boot;
    uint16_t part;
    if (!storagefmt::parseFileName(g.server->arg("before").c_str(), boot, part)) {
      sendSmallError("invalid file name");
      return;
    }
    scan->before = storagefmt::fileKey(boot, part);
  }
  SdSession sd;
  if (!sd.ok()) {
    sendSmallError(sysinfo::freeHeap() < kMinHeapForSd ? "not enough memory to read the SD card right now"
                                                          : "SD card not found or not readable");
    return;
  }
  sd.fs().list(storagefmt::kLogDir, onLogFile, scan.get());
  // newest first
  for (size_t i = 1; i < scan->count; ++i) {
    for (size_t j = i; j > 0 && scan->items[j].key > scan->items[j - 1].key; --j) {
      const LogEntry t = scan->items[j];
      scan->items[j] = scan->items[j - 1];
      scan->items[j - 1] = t;
    }
  }
  JsonDocument doc;
  doc["ok"] = true;
  doc["total"] = scan->total;
  doc["more"] = scan->older > scan->count;  // older files left for the next page
  uint64_t totalBytes = 0;
  uint64_t freeBytes = 0;
  if (sd.fs().space(totalBytes, freeBytes)) {
    doc["cardMB"] = static_cast<uint32_t>(totalBytes >> 20);
    doc["freeMB"] = static_cast<uint32_t>(freeBytes >> 20);
  }
  JsonArray files = doc["files"].to<JsonArray>();
  char path[storagefmt::kPathLen];
  for (size_t i = 0; i < scan->count; ++i) {
    sysinfo::watchdogFeed();
    snprintf(path, sizeof(path), "%s/%s", storagefmt::kLogDir, scan->items[i].name);
    JsonObject f = files.add<JsonObject>();
    f["name"] = scan->items[i].name;
    f["size"] = sd.fs().fileSize(path);
    // Last write (UTC), 0 if the box had no time then (no time sync in that boot).
    f["mtime"] = sd.fs().fileMtime(path);
  }
  scan.reset();
  auto buf = sysinfo::makeTempBuffer(measureJson(doc) + 1);
  if (!buf) {
    sendSmallError("out of memory");
    return;
  }
  const size_t len = serializeJson(doc, buf.get(), measureJson(doc) + 1);
  sendJson(200, buf.get(), len);
}

void pauseDns(bool pause);

// Streams one log file. The download blocks the main loop, so the watchdog is fed per chunk.
void handleSdLogFile() {
  g.lastActivityMs = millis();
  const String& name = g.server->arg("name");
  uint32_t boot;
  uint16_t part;
  if (!storagefmt::parseFileName(name.c_str(), boot, part) || !name.endsWith(".log") || name.length() >= 24) {
    sendSmallError("invalid file name");  // also rules out paths like ../
    return;
  }
  auto chunk = sysinfo::makeTempBuffer(kDownloadChunk);
  if (!chunk) {
    sendSmallError("out of memory");
    return;
  }
  SdSession sd;
  if (!sd.ok()) {
    sendSmallError("SD card not found, not readable or not enough memory");
    return;
  }
  char path[storagefmt::kPathLen];
  snprintf(path, sizeof(path), "%s/%s", storagefmt::kLogDir, name.c_str());
  const int64_t size = sd.fs().fileSize(path);
  FILE* f = size >= 0 ? sd.fs().openRead(path) : nullptr;
  if (f == nullptr) {
    sendSmallError("file not found");
    return;
  }
  LOGI(kTag, "download %s (%lu KB)", name.c_str(), static_cast<unsigned long>(size / 1024));
  pauseDns(true);
  g.server->sendHeader("Content-Disposition", String("attachment; filename=\"") + name + "\"");
  g.server->sendHeader("Cache-Control", "no-store");
  g.server->setContentLength(static_cast<size_t>(size));
  g.server->send(200, "text/plain; charset=utf-8", "");
  int64_t sent = 0;
  while (sent < size) {
    const size_t n = fread(chunk.get(), 1, kDownloadChunk, f);
    if (n == 0) break;
    g.server->sendContent(chunk.get(), n);
    sent += static_cast<int64_t>(n);
    sysinfo::watchdogFeed();
    g.lastActivityMs = millis();
    if (!g.server->client().connected()) break;
  }
  fclose(f);
  pauseDns(false);
  if (sent < size) LOGW(kTag, "download of %s stopped after %lu KB", name.c_str(), static_cast<unsigned long>(sent / 1024));
}

// Firmware upload, called by WebServer for every chunk of the multipart body. The whole upload
// runs inside one handleClient() call, so the watchdog is fed here.
void handleOtaChunk() {
  HTTPUpload& up = g.server->upload();
  const uint32_t now = millis();
  g.lastActivityMs = now;
  sysinfo::watchdogFeed();
  switch (up.status) {
    case UPLOAD_FILE_START:
      delete g.upload;
      g.upload = new (std::nothrow) updater::ImageUpload();
      g.uploadStartMs = now;
      g.uploadLastLogBytes = 0;
      g.uploadLastChunkMs = now;
      g.uploadMaxGapMs = 0;
      sysinfo::breadcrumb("ota:upload");  // shows up after a reset in the middle of the upload
      pauseDns(true);
      LOGI(kTag, "firmware upload started: %s, heap free %lu, largest block %lu", up.filename.c_str(),
           static_cast<unsigned long>(sysinfo::freeHeap()), static_cast<unsigned long>(sysinfo::largestFreeBlock()));
      if (g.upload != nullptr) g.upload->begin(g.options.boardRevision);
      break;
    case UPLOAD_FILE_WRITE: {
      const uint32_t gap = now - g.uploadLastChunkMs;
      if (gap > g.uploadMaxGapMs) g.uploadMaxGapMs = gap;
      g.uploadLastChunkMs = now;
      if (g.upload != nullptr) g.upload->write(up.buf, up.currentSize);
      if (up.totalSize - g.uploadLastLogBytes >= 128u * 1024u) {
        g.uploadLastLogBytes = up.totalSize;
        LOGI(kTag, "upload %lu KB, %lu KB/s, heap %lu/min %lu, max pause %lu ms",
             static_cast<unsigned long>(up.totalSize / 1024),
             static_cast<unsigned long>(up.totalSize / (now - g.uploadStartMs + 1)),
             static_cast<unsigned long>(sysinfo::freeHeap()), static_cast<unsigned long>(sysinfo::minFreeHeap()),
             static_cast<unsigned long>(g.uploadMaxGapMs));
      }
      break;
    }
    case UPLOAD_FILE_END:
      if (g.upload != nullptr && !g.upload->failed()) g.upload->end();
      sysinfo::breadcrumb("service");
      pauseDns(false);
      break;
    case UPLOAD_FILE_ABORTED:
      // WebServer aborts when the client disconnects or sends nothing for 5 s.
      LOGW(kTag, "upload aborted at %lu KB (disconnected/5 s no data); heap %lu/min %lu, max pause %lu ms",
           static_cast<unsigned long>(up.totalSize / 1024),
           static_cast<unsigned long>(sysinfo::freeHeap()), static_cast<unsigned long>(sysinfo::minFreeHeap()),
           static_cast<unsigned long>(g.uploadMaxGapMs));
      if (g.upload != nullptr) g.upload->abort();
      sysinfo::breadcrumb("service");
      pauseDns(false);
      break;
  }
}

// Called after the upload: answers and, on success, restarts into the new firmware.
void handleOtaDone() {
  auto buf = beginRequest();
  if (outOfMemory(buf)) return;
  if (g.upload == nullptr) {
    sendResult(false, "no firmware file received (or out of memory)");
    return;
  }
  const bool ok = !g.upload->failed() && g.upload->written() > 0;
  if (!ok) {
    LOGW(kTag, "firmware update refused: %s", g.upload->error());
    sendResult(false, g.upload->failed() ? g.upload->error() : "empty file");
  } else {
    JsonDocument doc;
    doc["ok"] = true;
    doc["version"] = g.upload->newVersion();
    doc["bytes"] = g.upload->written();
    const size_t len = serializeJson(doc, g.response, kResponseSize);
    sendJson(200, g.response, len);
    LOGI(kTag, "restarting into firmware %s", g.upload->newVersion());
    g.rebootPending = true;
    g.rebootAtMs = millis() + kRebootDelayMs;
  }
  delete g.upload;
  g.upload = nullptr;
}

// Captive portal: phones/laptops probe URLs like /generate_204 or /hotspot-detect.html.
// Redirecting everything unknown to the start page makes the OS open the web UI by itself.
// The OS repeats these probes on its own, so they do not count as activity.
void handleNotFound() {
  if (g.server->uri().startsWith("/api/")) {
    auto buf = beginRequest(false);
    if (outOfMemory(buf)) return;
    sendResult(false, "unknown API endpoint");
    return;
  }
  g.server->sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  g.server->send(302, "text/plain", "");
}

}  // namespace

void serviceApSsid(char* out, size_t cap, const char* serial) {
  if (serial != nullptr && serial[0] != '\0') {
    snprintf(out, cap, "senseBox-%.20s", serial);
    return;
  }
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(out, cap, "senseBox-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

bool ServiceMode::begin(const Options& options) {
  g.options = options;
  g.running = updater::bootInfo();
  serviceApSsid(g.ssid, sizeof(g.ssid), options.deviceSerial);

  char password[servicepw::kMaxLen + 1];
  bool isDefault = false;
  if (!servicepw::get(password, sizeof(password), isDefault)) {
    LOGE(kTag, "could not read the service password");
    return false;
  }

  WiFi.persistent(false);  // never write WiFi settings to flash
  if (!WiFi.mode(WIFI_AP) || !WiFi.softAP(g.ssid, password, 1, 0, kMaxClients)) {
    LOGE(kTag, "could not start the access point");
    return false;
  }
  // The password goes to the serial console only (physical access), never into the log ring.
  Serial.printf("\n*** service mode: WiFi \"%s\", password \"%s\"%s, then open http://%s/ ***\n\n", g.ssid, password,
                isDefault ? " (default)" : "", WiFi.softAPIP().toString().c_str());
  memset(password, 0, sizeof(password));

  g.dns = new NetworkUDP();
  if (!g.dns->begin(53)) LOGW(kTag, "captive DNS could not start; open http://%s/ manually", WiFi.softAPIP().toString().c_str());

  g.server = new WebServer(80);
  g.server->on("/", HTTP_GET, handleIndex);
  g.server->on("/api/status", HTTP_GET, handleStatus);
  g.server->on("/api/log", HTTP_GET, handleLog);
  g.server->on("/api/config", HTTP_GET, handleGetConfig);
  g.server->on("/api/config", HTTP_POST, handlePostConfig);
  g.server->on("/api/config/field", HTTP_POST, handlePostField);
  g.server->on("/api/appkey", HTTP_POST, handlePostAppKey);
  g.server->on("/api/service-password", HTTP_POST, handlePostServicePassword);
  g.server->on("/api/reboot", HTTP_POST, handleReboot);
  g.server->on("/api/ota", HTTP_POST, handleOtaDone, handleOtaChunk);
  g.server->on("/api/sdlogs", HTTP_GET, handleSdLogList);
  g.server->on("/api/sdlogs/file", HTTP_GET, handleSdLogFile);
  g.server->onNotFound(handleNotFound);
  g.server->begin();

  g.lastActivityMs = millis();
  g.noClientExit.begin(g.lastActivityMs, static_cast<uint32_t>(options.idleExitSec) * 1000u);
  LOGI(kTag, "access point \"%s\" up at %s, closes after %u min without activity", g.ssid,
       WiFi.softAPIP().toString().c_str(), g.options.apTimeoutMin);
  if (options.idleExitSec > 0) {
    LOGI(kTag, "also closes %u s after the last device left", options.idleExitSec);
  }
  const uint32_t freeHeap = sysinfo::freeHeap();
  LOGI(kTag, "internal heap free %lu, largest block %lu", static_cast<unsigned long>(freeHeap),
       static_cast<unsigned long>(sysinfo::largestFreeBlock()));
  if (freeHeap < kLowHeapWarning) LOGW(kTag, "low memory: WiFi connections may fail");
  return true;
}

namespace {
// Answers pending DNS queries (a few per call at most, so HTTP isn't starved).
void pollDns() {
  if (g.dns == nullptr) return;
  for (int i = 0; i < 4; ++i) {
    const int len = g.dns->parsePacket();
    if (len <= 0) return;
    uint8_t query[captivedns::kMaxPacket];
    uint8_t answer[captivedns::kMaxPacket];
    const int n = g.dns->read(query, sizeof(query));
    if (n <= 0) continue;
    const IPAddress ip = WiFi.softAPIP();
    const uint8_t ipv4[4] = {ip[0], ip[1], ip[2], ip[3]};
    const size_t out = captivedns::buildResponse(query, static_cast<size_t>(n), ipv4, answer, sizeof(answer));
    if (out == 0) continue;
    g.dns->beginPacket(g.dns->remoteIP(), g.dns->remotePort());
    g.dns->write(answer, out);
    g.dns->endPacket();
  }
}
}  // namespace

void ServiceMode::loop(uint32_t nowMs) {
  if (g.server == nullptr) return;
  pollDns();
  g.server->handleClient();
  g.response = nullptr;  // only valid during a request
  nowMs = millis();  // handlers update lastActivityMs with the current time
  // A device joining the AP counts as activity; staying connected does not.
  const uint8_t n = clients();
  if (n > g.lastClients) g.lastActivityMs = nowMs;
  g.lastClients = n;
  const bool nobodyLeft = g.noClientExit.update(n, nowMs);

  if (g.rebootPending && static_cast<int32_t>(nowMs - g.rebootAtMs) >= 0) {
    Serial.flush();
    ESP.restart();
  }
  if (secondsLeft(nowMs) == 0 || nobodyLeft) {
    if (nobodyLeft) {
      LOGI(kTag, "no device connected for %u s, restarting into normal mode", g.options.idleExitSec);
    } else {
      LOGI(kTag, "no activity for %u min, restarting into normal mode", g.options.apTimeoutMin);
    }
    Serial.flush();
    delay(100);
    ESP.restart();
  }
}

void ServiceMode::markActivity() { g.lastActivityMs = millis(); }

const char* ServiceMode::ssid() const { return g.ssid; }

uint8_t ServiceMode::clients() const { return g.server != nullptr ? WiFi.softAPgetStationNum() : 0; }

uint32_t ServiceMode::secondsLeft(uint32_t nowMs) const {
  const uint32_t timeoutMs = static_cast<uint32_t>(g.options.apTimeoutMin) * 60000u;
  if (static_cast<int32_t>(nowMs - g.lastActivityMs) < 0) return (timeoutMs + 999) / 1000;
  const uint32_t idleMs = nowMs - g.lastActivityMs;
  return idleMs >= timeoutMs ? 0 : (timeoutMs - idleMs + 999) / 1000;
}
