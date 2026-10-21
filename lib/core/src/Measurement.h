#pragma once

#include <stddef.h>
#include <stdint.h>

// Unified data model: every value in the firmware is a Measurement.
// See IMPLEMENTATION_PLAN.md §4.2.

enum class Phenomenon : uint8_t {
  Unknown = 0,
  AirTemperature,
  RelativeHumidity,
  PM1,
  PM2_5,
  PM4,
  PM10,
  Count
};

enum class Unit : uint8_t {
  None = 0,
  DegC,
  Percent,
  UgPerM3,
  Count
};

enum class ValueKind : uint8_t {
  Raw = 0,     // as delivered by the sensor
  Corrected,   // raw value after calibration (gain/offset)
  Aggregated,  // mean over an aggregation window
};

// Quality flags (bitmask). 0 = no findings.
namespace quality {
constexpr uint16_t kOutOfRange = 1 << 0;      // outside the sensor's physical range
constexpr uint16_t kImplausible = 1 << 1;     // outside the plausible environmental range
constexpr uint16_t kSpike = 1 << 2;           // jump bigger than allowed vs. previous value
constexpr uint16_t kStuck = 1 << 3;           // identical value for N readings
constexpr uint16_t kWarmup = 1 << 4;          // sensor still warming up
constexpr uint16_t kCommRetry = 1 << 5;       // value obtained after a bus retry
constexpr uint16_t kTimeUnsynced = 1 << 6;    // timestamp is uptime seconds, not UTC
constexpr uint16_t kIncompleteAgg = 1 << 7;   // aggregate built from fewer samples than expected
constexpr uint16_t kClamped = 1 << 8;         // value clamped to fit a transport encoding

// Flags that exclude a sample from aggregation.
constexpr uint16_t kExcludeFromAggregate = kOutOfRange | kImplausible | kSpike | kStuck | kWarmup;
}  // namespace quality

struct Measurement {
  // UTC epoch seconds, or uptime seconds if quality has kTimeUnsynced.
  uint32_t timestamp;
  float value;
  uint8_t sensorSlot;  // index of the configured sensor instance
  Phenomenon phenomenon;
  Unit unit;
  ValueKind kind;
  uint16_t quality;
};

static_assert(sizeof(Measurement) <= 16, "Measurement must stay small: it is stored by the thousands");

// Stable string names, used in config, REST API and CSV export. Never rename existing entries.
const char* phenomenonName(Phenomenon p);
const char* phenomenonShortName(Phenomenon p);
bool phenomenonFromName(const char* name, Phenomenon& out);
const char* unitSymbol(Unit u);
Unit defaultUnit(Phenomenon p);
const char* valueKindName(ValueKind k);
