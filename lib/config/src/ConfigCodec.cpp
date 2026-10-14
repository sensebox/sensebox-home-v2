#include "ConfigCodec.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <memory>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace config {
namespace {

bool fail(DecodeResult& r, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
bool fail(DecodeResult& r, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(r.error, sizeof(r.error), fmt, args);
  va_end(args);
  return false;
}

// Copies a string field. Missing/null keeps the default. Rejects non-strings and overlong values.
bool readString(JsonVariantConst v, char* dst, size_t cap, const char* field, DecodeResult& r) {
  if (v.isNull()) return true;
  if (!v.is<const char*>()) return fail(r, "%s: must be a string", field);
  const char* s = v.as<const char*>();
  if (strlen(s) >= cap) return fail(r, "%s: longer than %u characters", field, static_cast<unsigned>(cap - 1));
  strcpy(dst, s);
  return true;
}

template <typename T>
bool readUInt(JsonVariantConst v, T& dst, uint32_t min, uint32_t max, const char* field, DecodeResult& r) {
  if (v.isNull()) return true;
  if (!v.is<uint32_t>()) return fail(r, "%s: must be a non-negative integer", field);
  const uint32_t value = v.as<uint32_t>();
  if (value < min || value > max) {
    return fail(r, "%s: %u out of range %u..%u", field, static_cast<unsigned>(value), static_cast<unsigned>(min),
                static_cast<unsigned>(max));
  }
  dst = static_cast<T>(value);
  return true;
}

bool readFloat(JsonVariantConst v, float& dst, const char* field, DecodeResult& r) {
  if (v.isNull()) return true;
  if (!v.is<float>()) return fail(r, "%s: must be a number", field);
  dst = v.as<float>();
  return true;
}

bool readBool(JsonVariantConst v, bool& dst, const char* field, DecodeResult& r) {
  if (v.isNull()) return true;
  if (!v.is<bool>()) return fail(r, "%s: must be true or false", field);
  dst = v.as<bool>();
  return true;
}

bool readPhenomenon(JsonVariantConst v, Phenomenon& dst, const char* field, DecodeResult& r) {
  if (!v.is<const char*>() || !phenomenonFromName(v.as<const char*>(), dst)) {
    return fail(r, "%s: unknown phenomenon", field);
  }
  return true;
}

bool decodeSensor(JsonObjectConst obj, SensorConfig& s, size_t index, DecodeResult& r) {
  char field[48];
  for (JsonPairConst kv : obj) {
    const char* key = kv.key().c_str();
    snprintf(field, sizeof(field), "sensors[%u].%s", static_cast<unsigned>(index), key);
    JsonVariantConst v = kv.value();

    if (strcmp(key, "slot") == 0) {
      if (v.isNull()) return fail(r, "%s: required", field);
      if (!readUInt(v, s.slot, 0, kMaxSensors - 1, field, r)) return false;
    } else if (strcmp(key, "type") == 0) {
      if (!readString(v, s.type, sizeof(s.type), field, r)) return false;
    } else if (strcmp(key, "enabled") == 0) {
      if (!readBool(v, s.enabled, field, r)) return false;
    } else if (strcmp(key, "intervalSec") == 0) {
      if (!readUInt(v, s.intervalSec, 1, 86400, field, r)) return false;
    } else if (strcmp(key, "calibration") == 0) {
      if (!v.is<JsonObjectConst>()) return fail(r, "%s: must be an object", field);
      for (JsonPairConst cal : v.as<JsonObjectConst>()) {
        if (s.calibrationCount >= kMaxCalibrations) return fail(r, "%s: too many entries", field);
        Calibration& c = s.calibrations[s.calibrationCount];
        if (!phenomenonFromName(cal.key().c_str(), c.phenomenon)) return fail(r, "%s: unknown phenomenon", field);
        if (!cal.value().is<JsonObjectConst>()) return fail(r, "%s: entries must be objects", field);
        if (!readFloat(cal.value()["gain"], c.gain, field, r)) return false;
        if (!readFloat(cal.value()["offset"], c.offset, field, r)) return false;
        ++s.calibrationCount;
      }
    } else if (v.is<float>()) {
      // Any other numeric field is a driver-specific parameter.
      if (s.paramCount >= kMaxSensorParams) return fail(r, "%s: too many sensor parameters", field);
      if (strlen(key) >= kParamKeyLen) return fail(r, "%s: parameter name too long", field);
      SensorParam& p = s.params[s.paramCount++];
      strcpy(p.key, key);
      p.value = v.as<float>();
    } else {
      return fail(r, "%s: unknown field", field);
    }
  }
  if (obj["slot"].isNull()) return fail(r, "sensors[%u].slot: required", static_cast<unsigned>(index));
  if (s.type[0] == '\0') return fail(r, "sensors[%u].type: required", static_cast<unsigned>(index));
  return true;
}

bool decodeLoRaWan(JsonObjectConst obj, LoRaWanConfig& lw, const StationConfig& cfg, size_t index,
                   DecodeResult& r) {
  char field[48];
  snprintf(field, sizeof(field), "uplinks[%u].devEui", static_cast<unsigned>(index));
  if (!readString(obj["devEui"], lw.devEui, sizeof(lw.devEui), field, r)) return false;
  if (!isValidEui(lw.devEui)) return fail(r, "%s: must be 16 hex digits", field);
  snprintf(field, sizeof(field), "uplinks[%u].joinEui", static_cast<unsigned>(index));
  if (!readString(obj["joinEui"], lw.joinEui, sizeof(lw.joinEui), field, r)) return false;
  if (!isValidEui(lw.joinEui)) return fail(r, "%s: must be 16 hex digits", field);
  snprintf(field, sizeof(field), "uplinks[%u].intervalSec", static_cast<unsigned>(index));
  if (!readUInt(obj["intervalSec"], lw.intervalSec, 60, 86400, field, r)) return false;
  snprintf(field, sizeof(field), "uplinks[%u].timeSyncIntervalSec", static_cast<unsigned>(index));
  if (!readUInt(obj["timeSyncIntervalSec"], lw.timeSyncIntervalSec, 3600, 30u * 86400u, field, r)) return false;

  JsonVariantConst channels = obj["channels"];
  if (channels.isNull()) return true;
  if (!channels.is<JsonArrayConst>()) return fail(r, "uplinks[%u].channels: must be an array", static_cast<unsigned>(index));
  for (JsonObjectConst ch : channels.as<JsonArrayConst>()) {
    snprintf(field, sizeof(field), "uplinks[%u].channels[%u]", static_cast<unsigned>(index),
             static_cast<unsigned>(lw.channelCount));
    if (lw.channelCount >= kMaxLppChannels) return fail(r, "%s: too many channels", field);
    LppChannel& c = lw.channels[lw.channelCount];
    if (ch["ch"].isNull() || ch["slot"].isNull()) return fail(r, "%s: ch and slot are required", field);
    if (!readUInt(ch["ch"], c.channel, 0, 255, field, r)) return false;
    if (!readUInt(ch["slot"], c.slot, 0, kMaxSensors - 1, field, r)) return false;
    if (!readPhenomenon(ch["phenomenon"], c.phenomenon, field, r)) return false;
    if (!ch["lpp"].is<const char*>() || !lppTypeFromName(ch["lpp"].as<const char*>(), c.type)) {
      return fail(r, "%s: unknown lpp type", field);
    }
    if (cfg.sensorBySlot(c.slot) == nullptr) return fail(r, "%s: slot %u is not configured", field, c.slot);
    for (uint8_t i = 0; i < lw.channelCount; ++i) {
      if (lw.channels[i].channel == c.channel) return fail(r, "%s: duplicate channel %u", field, c.channel);
    }
    ++lw.channelCount;
  }
  return true;
}

bool decodeRoot(JsonObjectConst root, StationConfig& cfg, DecodeResult& r) {
  JsonObjectConst station = root["station"];
  if (!readString(station["name"], cfg.name, sizeof(cfg.name), "station.name", r)) return false;
  if (!readString(station["timezone"], cfg.timezone, sizeof(cfg.timezone), "station.timezone", r)) return false;

  JsonVariantConst sensors = root["sensors"];
  if (!sensors.isNull()) {
    if (!sensors.is<JsonArrayConst>()) return fail(r, "sensors: must be an array");
    for (JsonVariantConst item : sensors.as<JsonArrayConst>()) {
      if (cfg.sensorCount >= kMaxSensors) return fail(r, "sensors: more than %u", static_cast<unsigned>(kMaxSensors));
      if (!item.is<JsonObjectConst>()) return fail(r, "sensors[%u]: must be an object", cfg.sensorCount);
      SensorConfig& s = cfg.sensors[cfg.sensorCount];
      if (!decodeSensor(item.as<JsonObjectConst>(), s, cfg.sensorCount, r)) return false;
      for (uint8_t i = 0; i < cfg.sensorCount; ++i) {
        if (cfg.sensors[i].slot == s.slot) return fail(r, "sensors[%u]: duplicate slot %u", cfg.sensorCount, s.slot);
      }
      ++cfg.sensorCount;
    }
  }

  JsonVariantConst uplinks = root["uplinks"];
  if (!uplinks.isNull()) {
    if (!uplinks.is<JsonArrayConst>()) return fail(r, "uplinks: must be an array");
    for (JsonVariantConst item : uplinks.as<JsonArrayConst>()) {
      if (cfg.uplinkCount >= kMaxUplinks) return fail(r, "uplinks: more than %u", static_cast<unsigned>(kMaxUplinks));
      UplinkConfig& u = cfg.uplinks[cfg.uplinkCount];
      if (!item["type"].is<const char*>() || !uplinkTypeFromName(item["type"].as<const char*>(), u.type)) {
        return fail(r, "uplinks[%u].type: unknown", cfg.uplinkCount);
      }
      if (u.type == UplinkType::LoRaWanCayenne &&
          !decodeLoRaWan(item.as<JsonObjectConst>(), u.lorawan, cfg, cfg.uplinkCount, r)) {
        return false;
      }
      ++cfg.uplinkCount;
    }
  }

  JsonObjectConst storage = root["storage"];
  if (!storage.isNull()) {
    JsonVariantConst sd = storage["sd"];
    if (!sd.isNull()) {
      if (sd == "auto") {
        cfg.storage.sd = SdMode::Auto;
      } else if (sd == "off") {
        cfg.storage.sd = SdMode::Off;
      } else {
        return fail(r, "storage.sd: must be \"auto\" or \"off\"");
      }
    }
    if (!readUInt(storage["lowWatermarkPct"], cfg.storage.lowWatermarkPct, 1, 90, "storage.lowWatermarkPct", r)) return false;
    if (!readUInt(storage["highWatermarkPct"], cfg.storage.highWatermarkPct, 2, 95, "storage.highWatermarkPct", r)) return false;
    if (!readUInt(storage["minRetentionDays"], cfg.storage.minRetentionDays, 0, 3650, "storage.minRetentionDays", r)) return false;
    if (cfg.storage.lowWatermarkPct >= cfg.storage.highWatermarkPct) {
      return fail(r, "storage: lowWatermarkPct must be below highWatermarkPct");
    }
  }

  JsonObjectConst service = root["service"];
  if (!readUInt(service["apTimeoutMin"], cfg.service.apTimeoutMin, 1, 240, "service.apTimeoutMin", r)) return false;

  JsonVariantConst deployment = root["deployment"];
  if (!deployment.isNull()) {
    if (!deployment.is<JsonObjectConst>()) return fail(r, "deployment: must be an object");
    if (measureJson(deployment) >= sizeof(cfg.deploymentJson)) return fail(r, "deployment: too large");
    serializeJson(deployment, cfg.deploymentJson, sizeof(cfg.deploymentJson));
  }
  return true;
}

}  // namespace

bool isValidEui(const char* hex) {
  const size_t len = strlen(hex);
  if (len == 0) return true;
  if (len != kEuiHexLen) return false;
  for (size_t i = 0; i < len; ++i) {
    if (!isxdigit(static_cast<unsigned char>(hex[i]))) return false;
  }
  return true;
}

DecodeResult decode(const char* json, size_t len, StationConfig& out, const MigrationTable& table) {
  DecodeResult r;
  JsonDocument doc;
  const DeserializationError jsonErr = deserializeJson(doc, json, len);
  if (jsonErr) {
    fail(r, "invalid JSON: %s", jsonErr.c_str());
    return r;
  }
  if (!doc.is<JsonObject>()) {
    fail(r, "config must be a JSON object");
    return r;
  }

  const MigrationStatus ms = migrate(doc.as<JsonObject>(), kSchemaVersion, table, r.error, sizeof(r.error), &r.fromVersion);
  if (ms != MigrationStatus::Current && ms != MigrationStatus::Migrated) return r;
  r.migrated = ms == MigrationStatus::Migrated;

  // Decode into a scratch copy so `out` stays valid if validation fails half-way. The copy is on
  // the heap only while decoding (config changes are rare; static RAM is scarce without PSRAM).
  std::unique_ptr<StationConfig> scratch(new (std::nothrow) StationConfig());
  if (!scratch) {
    fail(r, "out of memory");
    return r;
  }
  if (!decodeRoot(doc.as<JsonObjectConst>(), *scratch, r)) return r;
  scratch->schemaVersion = kSchemaVersion;
  out = *scratch;
  r.ok = true;
  return r;
}

size_t encode(const StationConfig& cfg, char* buf, size_t cap, bool pretty) {
  if (cap > 0) buf[0] = '\0';
  JsonDocument doc;
  doc["schemaVersion"] = cfg.schemaVersion;
  JsonObject station = doc["station"].to<JsonObject>();
  station["name"] = cfg.name;
  station["timezone"] = cfg.timezone;

  JsonArray sensors = doc["sensors"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.sensorCount; ++i) {
    const SensorConfig& s = cfg.sensors[i];
    JsonObject o = sensors.add<JsonObject>();
    o["slot"] = s.slot;
    o["type"] = s.type;
    o["enabled"] = s.enabled;
    o["intervalSec"] = s.intervalSec;
    for (uint8_t p = 0; p < s.paramCount; ++p) o[s.params[p].key] = s.params[p].value;
    if (s.calibrationCount > 0) {
      JsonObject cal = o["calibration"].to<JsonObject>();
      for (uint8_t c = 0; c < s.calibrationCount; ++c) {
        JsonObject e = cal[phenomenonName(s.calibrations[c].phenomenon)].to<JsonObject>();
        e["gain"] = s.calibrations[c].gain;
        e["offset"] = s.calibrations[c].offset;
      }
    }
  }

  JsonArray uplinks = doc["uplinks"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.uplinkCount; ++i) {
    const UplinkConfig& u = cfg.uplinks[i];
    JsonObject o = uplinks.add<JsonObject>();
    o["type"] = uplinkTypeName(u.type);
    if (u.type == UplinkType::LoRaWanCayenne) {
      o["devEui"] = u.lorawan.devEui;
      o["joinEui"] = u.lorawan.joinEui;
      o["intervalSec"] = u.lorawan.intervalSec;
      o["timeSyncIntervalSec"] = u.lorawan.timeSyncIntervalSec;
      JsonArray chs = o["channels"].to<JsonArray>();
      for (uint8_t c = 0; c < u.lorawan.channelCount; ++c) {
        const LppChannel& lc = u.lorawan.channels[c];
        JsonObject co = chs.add<JsonObject>();
        co["ch"] = lc.channel;
        co["slot"] = lc.slot;
        co["phenomenon"] = phenomenonName(lc.phenomenon);
        co["lpp"] = lppTypeName(lc.type);
      }
    }
  }

  JsonObject storage = doc["storage"].to<JsonObject>();
  storage["sd"] = cfg.storage.sd == SdMode::Off ? "off" : "auto";
  storage["lowWatermarkPct"] = cfg.storage.lowWatermarkPct;
  storage["highWatermarkPct"] = cfg.storage.highWatermarkPct;
  storage["minRetentionDays"] = cfg.storage.minRetentionDays;

  doc["service"]["apTimeoutMin"] = cfg.service.apTimeoutMin;

  JsonDocument deployment;
  if (deserializeJson(deployment, cfg.deploymentJson) == DeserializationError::Ok) doc["deployment"] = deployment;

  const size_t needed = pretty ? measureJsonPretty(doc) : measureJson(doc);
  if (needed + 1 > cap) {
    if (cap > 0) buf[0] = '\0';
    return 0;
  }
  return pretty ? serializeJsonPretty(doc, buf, cap) : serializeJson(doc, buf, cap);
}

}  // namespace config
