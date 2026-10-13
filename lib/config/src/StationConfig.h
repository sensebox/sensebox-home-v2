#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Measurement.h"

// Typed station configuration. The rest of the firmware only sees these structs, never JSON.
// JSON <-> struct conversion: ConfigCodec. Schema history: ConfigMigrator. See plan §5.
//
// Secrets (LoRaWAN AppKey, ...) are NOT part of this struct; they live in a separate store.

namespace config {

constexpr uint16_t kSchemaVersion = 1;

constexpr size_t kMaxSensors = 8;
constexpr size_t kMaxSensorParams = 4;
constexpr size_t kMaxCalibrations = 4;
constexpr size_t kMaxUplinks = 2;
constexpr size_t kMaxLppChannels = 16;

constexpr size_t kNameLen = 32;
constexpr size_t kTypeLen = 16;
constexpr size_t kParamKeyLen = 24;
constexpr size_t kEuiHexLen = 16;  // 8 bytes as hex, MSB first
constexpr size_t kDeploymentJsonLen = 512;

struct Calibration {
  Phenomenon phenomenon = Phenomenon::Unknown;
  float gain = 1.0f;
  float offset = 0.0f;
};

// Generic per-sensor option, interpreted by the driver (e.g. "fanCleaningIntervalDays").
struct SensorParam {
  char key[kParamKeyLen] = {};
  float value = 0.0f;
};

struct SensorConfig {
  uint8_t slot = 0;
  char type[kTypeLen] = {};  // driver registry key, e.g. "am2315c", "sps30"
  bool enabled = true;
  uint32_t intervalSec = 60;
  Calibration calibrations[kMaxCalibrations];
  uint8_t calibrationCount = 0;
  SensorParam params[kMaxSensorParams];
  uint8_t paramCount = 0;

  // Returns the param value or `fallback` if not set.
  float param(const char* key, float fallback) const;
  // Returns the calibration for `p`, or nullptr if none is configured.
  const Calibration* calibration(Phenomenon p) const;
};

// Cayenne LPP data types usable with TTN's CayenneLPP payload formatter.
enum class LppType : uint8_t {
  Unknown = 0,
  DigitalInput,      // 0x00, 1 byte
  AnalogInput,       // 0x02, 2 bytes, 0.01 signed
  Illuminance,       // 0x65, 2 bytes, 1 unsigned
  Temperature,       // 0x67, 2 bytes, 0.1 signed
  RelativeHumidity,  // 0x68, 1 byte, 0.5 unsigned
  BarometricPressure // 0x73, 2 bytes, 0.1 unsigned
};

const char* lppTypeName(LppType t);
bool lppTypeFromName(const char* name, LppType& out);

struct LppChannel {
  uint8_t channel = 0;
  uint8_t slot = 0;
  Phenomenon phenomenon = Phenomenon::Unknown;
  LppType type = LppType::Unknown;
};

enum class UplinkType : uint8_t { None = 0, LoRaWanCayenne };

const char* uplinkTypeName(UplinkType t);
bool uplinkTypeFromName(const char* name, UplinkType& out);

struct LoRaWanConfig {
  char devEui[kEuiHexLen + 1] = {};   // not secret
  char joinEui[kEuiHexLen + 1] = {};  // not secret
  uint32_t intervalSec = 600;
  uint32_t timeSyncIntervalSec = 72u * 3600u;
  LppChannel channels[kMaxLppChannels];
  uint8_t channelCount = 0;
};

struct UplinkConfig {
  UplinkType type = UplinkType::None;
  LoRaWanConfig lorawan;
};

enum class SdMode : uint8_t { Auto = 0, Off };

struct StorageConfig {
  SdMode sd = SdMode::Auto;
  uint8_t lowWatermarkPct = 10;
  uint8_t highWatermarkPct = 20;
  uint16_t minRetentionDays = 30;
};

struct ServiceConfig {
  uint16_t apTimeoutMin = 15;
};

struct StationConfig {
  uint16_t schemaVersion = kSchemaVersion;
  char name[kNameLen] = {};
  char timezone[kNameLen] = "Europe/Berlin";
  SensorConfig sensors[kMaxSensors];
  uint8_t sensorCount = 0;
  UplinkConfig uplinks[kMaxUplinks];
  uint8_t uplinkCount = 0;
  StorageConfig storage;
  ServiceConfig service;
  // Free-form deployment metadata (location, mounting height, shield, ...) as compact JSON.
  char deploymentJson[kDeploymentJsonLen] = "{}";

  const SensorConfig* sensorBySlot(uint8_t slot) const;
};

// An unconfigured station: no sensors, no uplinks.
void setDefaults(StationConfig& cfg);

}  // namespace config
