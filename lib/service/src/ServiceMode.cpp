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
#include "ServicePassword.h"
#include "SystemInfo.h"
#include "Version.h"
#include "WebUi.h"

namespace {

constexpr const char* kTag = "service";
constexpr size_t kResponseSize = 6144;  // per request, freed afterwards
constexpr size_t kLiveLogLines = 20;
constexpr uint8_t kMaxClients = 2;  // each WiFi station costs RAM
constexpr uint32_t kRebootDelayMs = 500;
constexpr uint32_t kLowHeapWarning = 30000;

// Server objects live on the heap and are created in begin(): normal mode never pays for them.
// Response buffers are allocated per request (no PSRAM: nothing big stays allocated).
struct Context {
  ServiceMode::Options options;
  WebServer* server = nullptr;
  NetworkUDP* dns = nullptr;  // captive DNS, polled from the main loop (no extra task)
  char* response = nullptr;   // valid during one request
  char ssid[33] = {};
  uint32_t lastActivityMs = 0;
  uint32_t rebootAtMs = 0;
  bool rebootPending = false;
};
Context g;

// Starts a request: marks activity and allocates the response buffer for this request.
// Returns the owner; g.response points into it until it goes out of scope.
std::unique_ptr<char[]> beginRequest() {
  g.lastActivityMs = millis();
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
  auto buf = beginRequest();
  if (outOfMemory(buf)) return;
  const uint32_t now = millis();
  ServiceMode mode;
  int n = snprintf(g.response, kResponseSize,
                   "{\"mode\":\"service\",\"firmware\":\"%s\",\"service\":{\"ssid\":\"%s\",\"clients\":%u,"
                   "\"uptimeS\":%lu,\"secondsLeft\":%lu,\"apTimeoutMin\":%u,\"freeHeap\":%lu,\"minFreeHeap\":%lu},"
                   "\"snapshot\":",
                   SBH_FW_VERSION, g.ssid, mode.clients(), static_cast<unsigned long>(sysinfo::uptimeMs() / 1000),
                   static_cast<unsigned long>(mode.secondsLeft(now)), g.options.apTimeoutMin,
                   static_cast<unsigned long>(sysinfo::freeHeap()), static_cast<unsigned long>(sysinfo::minFreeHeap()));
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
  JsonArray now = doc["now"].to<JsonArray>();
  const size_t total = logger::lineCount();
  for (size_t age = total < kLiveLogLines ? total : kLiveLogLines; age > 0; --age) {
    LogLine line;
    if (!logger::lineAt(age - 1, line)) continue;
    JsonObject o = now.add<JsonObject>();
    o["uptimeS"] = line.uptimeS;
    char level[2] = {logger::levelLetter(line.level), '\0'};
    o["level"] = level;
    o["tag"] = line.tag;
    o["msg"] = line.msg;
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

void handleReboot() {
  auto buf = beginRequest();
  if (outOfMemory(buf)) return;
  sendResult(true, "");
  LOGI(kTag, "reboot requested from the web UI");
  g.rebootPending = true;
  g.rebootAtMs = millis() + kRebootDelayMs;  // let the response go out first
}

// Captive portal: phones/laptops probe URLs like /generate_204 or /hotspot-detect.html.
// Redirecting everything unknown to the start page makes the OS open the web UI by itself.
void handleNotFound() {
  if (g.server->uri().startsWith("/api/")) {
    auto buf = beginRequest();
    if (outOfMemory(buf)) return;
    sendResult(false, "unknown API endpoint");
    return;
  }
  g.lastActivityMs = millis();
  g.server->sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  g.server->send(302, "text/plain", "");
}

void buildSsid(const char* serial) {
  if (serial != nullptr && serial[0] != '\0') {
    snprintf(g.ssid, sizeof(g.ssid), "senseBox-%.20s", serial);
    return;
  }
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(g.ssid, sizeof(g.ssid), "senseBox-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

}  // namespace

bool ServiceMode::begin(const Options& options) {
  g.options = options;
  buildSsid(options.deviceSerial);

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
  g.server->on("/api/reboot", HTTP_POST, handleReboot);
  g.server->onNotFound(handleNotFound);
  g.server->begin();

  g.lastActivityMs = millis();
  LOGI(kTag, "access point \"%s\" up at %s, closes after %u min without activity", g.ssid,
       WiFi.softAPIP().toString().c_str(), g.options.apTimeoutMin);
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
  if (clients() > 0) g.lastActivityMs = nowMs;  // a connected phone counts as activity

  if (g.rebootPending && static_cast<int32_t>(nowMs - g.rebootAtMs) >= 0) {
    Serial.flush();
    ESP.restart();
  }
  if (secondsLeft(nowMs) == 0) {
    LOGI(kTag, "no activity for %u min, restarting into normal mode", g.options.apTimeoutMin);
    Serial.flush();
    delay(100);
    ESP.restart();
  }
}

const char* ServiceMode::ssid() const { return g.ssid; }

uint8_t ServiceMode::clients() const { return g.server != nullptr ? WiFi.softAPgetStationNum() : 0; }

uint32_t ServiceMode::secondsLeft(uint32_t nowMs) const {
  const uint32_t timeoutMs = static_cast<uint32_t>(g.options.apTimeoutMin) * 60000u;
  if (static_cast<int32_t>(nowMs - g.lastActivityMs) < 0) return (timeoutMs + 999) / 1000;
  const uint32_t idleMs = nowMs - g.lastActivityMs;
  return idleMs >= timeoutMs ? 0 : (timeoutMs - idleMs + 999) / 1000;
}
