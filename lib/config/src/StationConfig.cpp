#include "StationConfig.h"

#include <string.h>

namespace config {
namespace {

struct LppTypeInfo {
  LppType type;
  const char* name;  // TTN CayenneLPP formatter field prefix
};

constexpr LppTypeInfo kLppTypes[] = {
    {LppType::DigitalInput, "digital_in"},
    {LppType::AnalogInput, "analog_in"},
    {LppType::Illuminance, "luminosity"},
    {LppType::Temperature, "temperature"},
    {LppType::RelativeHumidity, "relative_humidity"},
    {LppType::BarometricPressure, "barometric_pressure"},
};

}  // namespace

float SensorConfig::param(const char* key, float fallback) const {
  for (uint8_t i = 0; i < paramCount; ++i) {
    if (strcmp(params[i].key, key) == 0) return params[i].value;
  }
  return fallback;
}

const Calibration* SensorConfig::calibration(Phenomenon p) const {
  for (uint8_t i = 0; i < calibrationCount; ++i) {
    if (calibrations[i].phenomenon == p) return &calibrations[i];
  }
  return nullptr;
}

const SensorConfig* StationConfig::sensorBySlot(uint8_t slot) const {
  for (uint8_t i = 0; i < sensorCount; ++i) {
    if (sensors[i].slot == slot) return &sensors[i];
  }
  return nullptr;
}

const char* lppTypeName(LppType t) {
  for (const auto& info : kLppTypes) {
    if (info.type == t) return info.name;
  }
  return "unknown";
}

bool lppTypeFromName(const char* name, LppType& out) {
  if (name == nullptr) return false;
  for (const auto& info : kLppTypes) {
    if (strcmp(info.name, name) == 0) {
      out = info.type;
      return true;
    }
  }
  return false;
}

const char* uplinkTypeName(UplinkType t) {
  switch (t) {
    case UplinkType::LoRaWanCayenne: return "lorawan-cayenne";
    case UplinkType::LoRaWanCompact: return "lorawan-compact";
    case UplinkType::None: return "none";
  }
  return "none";
}

bool uplinkTypeFromName(const char* name, UplinkType& out) {
  if (name == nullptr) return false;
  if (strcmp(name, "lorawan-cayenne") == 0) {
    out = UplinkType::LoRaWanCayenne;
    return true;
  }
  if (strcmp(name, "lorawan-compact") == 0) {
    out = UplinkType::LoRaWanCompact;
    return true;
  }
  if (strcmp(name, "none") == 0) {
    out = UplinkType::None;
    return true;
  }
  return false;
}

void setDefaults(StationConfig& cfg) { cfg = StationConfig{}; }

}  // namespace config
