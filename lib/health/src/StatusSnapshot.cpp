#include "StatusSnapshot.h"

#include <ArduinoJson.h>
#include <string.h>

namespace snapshot {
namespace {

// Checksum over everything after the crc field.
constexpr size_t kCrcStart = offsetof(StatusSnapshot, crc) + sizeof(uint32_t);

uint32_t bodyCrc(const StatusSnapshot& s) {
  return crc32(reinterpret_cast<const uint8_t*>(&s) + kCrcStart, sizeof(StatusSnapshot) - kCrcStart);
}

}  // namespace

uint32_t crc32(const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= p[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

void clear(StatusSnapshot& s) { memset(&s, 0, sizeof(s)); }

void seal(StatusSnapshot& s) {
  s.magic = StatusSnapshot::kMagic;
  s.version = StatusSnapshot::kVersion;
  s.size = static_cast<uint16_t>(sizeof(StatusSnapshot));
  s.crc = bodyCrc(s);
}

bool isValid(const StatusSnapshot& s) {
  return s.magic == StatusSnapshot::kMagic && s.version == StatusSnapshot::kVersion &&
         s.size == sizeof(StatusSnapshot) && s.crc == bodyCrc(s) && s.componentCount <= StatusSnapshot::kMaxComponents &&
         s.sensorCount <= StatusSnapshot::kMaxSensors && s.valueCount <= StatusSnapshot::kMaxValues &&
         s.logCount <= StatusSnapshot::kMaxLogLines;
}

void invalidate(StatusSnapshot& s) { s.magic = 0; }

void copyText(char* dst, size_t cap, const char* src) {
  if (cap == 0) return;
  if (src == nullptr) src = "";
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

size_t toJson(const StatusSnapshot& s, char* buf, size_t cap, bool withLog) {
  if (cap > 0) buf[0] = '\0';
  JsonDocument doc;
  doc["firmware"] = s.firmware;
  doc["station"] = s.stationName;
  doc["resetReason"] = s.resetReason;
  doc["previousBreadcrumb"] = s.previousBreadcrumb;
  doc["uptimeS"] = s.uptimeS;
  if (s.utc != 0) {
    doc["utc"] = s.utc;
  } else {
    doc["utc"] = nullptr;
  }
  doc["safeMode"] = s.safeMode;
  doc["overall"] = s.overall;

  JsonObject heap = doc["heap"].to<JsonObject>();
  heap["free"] = s.freeHeap;
  heap["minFree"] = s.minFreeHeap;
  heap["largestBlock"] = s.largestFreeBlock;

  JsonArray components = doc["components"].to<JsonArray>();
  for (uint8_t i = 0; i < s.componentCount; ++i) {
    const SnapshotComponent& c = s.components[i];
    JsonObject o = components.add<JsonObject>();
    o["name"] = c.name;
    o["status"] = c.status;
    o["errorCount"] = c.errorCount;
    o["lastError"] = c.lastError;
    o["lastErrorUptimeS"] = c.lastErrorUptimeS;
    o["lastSuccessUptimeS"] = c.lastSuccessUptimeS;
  }

  JsonArray sensors = doc["sensors"].to<JsonArray>();
  for (uint8_t i = 0; i < s.sensorCount; ++i) {
    const SnapshotSensor& x = s.sensors[i];
    JsonObject o = sensors.add<JsonObject>();
    o["slot"] = x.slot;
    o["type"] = x.type;
    o["state"] = x.state;
    o["lastError"] = x.lastError;
    o["errorCount"] = x.errorCount;
    o["readingCount"] = x.readingCount;
    o["lastGoodUptimeS"] = x.lastGoodUptimeS;
    o["retryInS"] = x.retryInS;
  }

  JsonArray values = doc["values"].to<JsonArray>();
  for (uint8_t i = 0; i < s.valueCount; ++i) {
    const SnapshotValue& v = s.values[i];
    JsonObject o = values.add<JsonObject>();
    o["slot"] = v.slot;
    o["phenomenon"] = v.phenomenon;
    o["unit"] = v.unit;
    o["value"] = v.value;
    o["quality"] = v.quality;
    o["timestamp"] = v.timestamp;
  }

  JsonObject lw = doc["lorawan"].to<JsonObject>();
  lw["configured"] = s.lorawan.configured;
  lw["appKeySet"] = s.lorawan.appKeySet;
  lw["joined"] = s.lorawan.joined;
  lw["joinAttempts"] = s.lorawan.joinAttempts;
  lw["uplinks"] = s.lorawan.uplinks;
  lw["downlinks"] = s.lorawan.downlinks;
  lw["spreadingFactor"] = s.lorawan.spreadingFactor;
  lw["lastRssi"] = s.lorawan.lastRssi;
  lw["lastSnr"] = s.lorawan.lastSnr;
  lw["budgetMs"] = s.lorawan.budgetMs;

  if (withLog) {
    JsonArray log = doc["log"].to<JsonArray>();
    for (uint8_t i = 0; i < s.logCount; ++i) {
      const SnapshotLogLine& l = s.log[i];
      JsonObject o = log.add<JsonObject>();
      o["uptimeS"] = l.uptimeS;
      char level[2] = {l.level, '\0'};
      o["level"] = level;
      o["tag"] = l.tag;
      o["msg"] = l.msg;
    }
  }

  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, buf, cap);
}

}  // namespace snapshot
