#include "ConfigEdit.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "ConfigCodec.h"

namespace config {
namespace {

bool equalsIgnoreCase(const char* a, const char* b) { return a != nullptr && b != nullptr && strcasecmp(a, b) == 0; }

LoRaWanConfig* firstLoRaWan(StationConfig& cfg) {
  for (uint8_t i = 0; i < cfg.uplinkCount; ++i) {
    if (isLoRaWan(cfg.uplinks[i].type)) return &cfg.uplinks[i].lorawan;
  }
  return nullptr;
}

// Normalises an EUI: drops separators, upper-cases. Returns false if the result isn't 16 hex digits.
bool normaliseEui(const char* in, char out[kEuiHexLen + 1]) {
  size_t n = 0;
  for (const char* p = in; *p != '\0'; ++p) {
    if (*p == ' ' || *p == ':' || *p == '-') continue;
    if (!isxdigit(static_cast<unsigned char>(*p)) || n >= kEuiHexLen) return false;
    out[n++] = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
  }
  out[n] = '\0';
  return n == kEuiHexLen;
}

}  // namespace

Field fieldFromPath(const char* group, const char* key) {
  if (group == nullptr || group[0] == '\0') {
    if (equalsIgnoreCase(key, "name")) return Field::Name;
    return Field::Unknown;
  }
  if (equalsIgnoreCase(group, "station") && equalsIgnoreCase(key, "name")) return Field::Name;
  if (equalsIgnoreCase(group, "uplinks") || equalsIgnoreCase(group, "lorawan")) {
    if (equalsIgnoreCase(key, "devEui")) return Field::LoRaWanDevEui;
    if (equalsIgnoreCase(key, "joinEui") || equalsIgnoreCase(key, "appEui")) return Field::LoRaWanJoinEui;
  }
  return Field::Unknown;
}

bool setField(StationConfig& cfg, Field field, const char* value, char* err, size_t errLen) {
  if (value == nullptr) value = "";
  switch (field) {
    case Field::Name: {
      const size_t len = strlen(value);
      if (len == 0) {
        snprintf(err, errLen, "name must not be empty");
        return false;
      }
      if (len >= kNameLen) {
        snprintf(err, errLen, "name longer than %u characters", static_cast<unsigned>(kNameLen - 1));
        return false;
      }
      strcpy(cfg.name, value);
      return true;
    }
    case Field::LoRaWanDevEui:
    case Field::LoRaWanJoinEui: {
      LoRaWanConfig* lw = firstLoRaWan(cfg);
      if (lw == nullptr) {
        snprintf(err, errLen, "no LoRaWAN uplink configured");
        return false;
      }
      char eui[kEuiHexLen + 1];
      if (!normaliseEui(value, eui)) {
        snprintf(err, errLen, "EUI must be 16 hex digits (MSB first, as shown in the TTN console)");
        return false;
      }
      strcpy(field == Field::LoRaWanDevEui ? lw->devEui : lw->joinEui, eui);
      return true;
    }
    case Field::Unknown: break;
  }
  snprintf(err, errLen, "unknown field");
  return false;
}

}  // namespace config
