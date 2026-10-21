#include "Measurement.h"

#include <string.h>

namespace {

struct PhenomenonInfo {
  Phenomenon phenomenon;
  const char* name;
  const char* shortName;  // prefix of per-sensor quality overrides, e.g. "rh.stuckCount"
  Unit unit;
};

constexpr PhenomenonInfo kPhenomena[] = {
    {Phenomenon::AirTemperature, "air_temperature", "temp", Unit::DegC},
    {Phenomenon::RelativeHumidity, "relative_humidity", "rh", Unit::Percent},
    {Phenomenon::PM1, "pm1", "pm1", Unit::UgPerM3},
    {Phenomenon::PM2_5, "pm2_5", "pm2_5", Unit::UgPerM3},
    {Phenomenon::PM4, "pm4", "pm4", Unit::UgPerM3},
    {Phenomenon::PM10, "pm10", "pm10", Unit::UgPerM3},
};

}  // namespace

const char* phenomenonName(Phenomenon p) {
  for (const auto& info : kPhenomena) {
    if (info.phenomenon == p) return info.name;
  }
  return "unknown";
}

const char* phenomenonShortName(Phenomenon p) {
  for (const auto& info : kPhenomena) {
    if (info.phenomenon == p) return info.shortName;
  }
  return "unknown";
}

bool phenomenonFromName(const char* name, Phenomenon& out) {
  if (name == nullptr) return false;
  for (const auto& info : kPhenomena) {
    if (strcmp(info.name, name) == 0) {
      out = info.phenomenon;
      return true;
    }
  }
  return false;
}

const char* unitSymbol(Unit u) {
  switch (u) {
    case Unit::DegC: return "°C";
    case Unit::Percent: return "%";
    case Unit::UgPerM3: return "µg/m³";
    default: return "";
  }
}

Unit defaultUnit(Phenomenon p) {
  for (const auto& info : kPhenomena) {
    if (info.phenomenon == p) return info.unit;
  }
  return Unit::None;
}

const char* valueKindName(ValueKind k) {
  switch (k) {
    case ValueKind::Raw: return "raw";
    case ValueKind::Corrected: return "corrected";
    case ValueKind::Aggregated: return "aggregated";
  }
  return "unknown";
}
