#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Measurement.h"
#include "StationConfig.h"

// Sensor driver interface (plan §4.3).
//
// Drivers are state machines driven by tick(). They never wait without a bound and never
// loop forever. Errors are reported through state() == Error; the SensorManager handles retry
// and backoff. Drivers know nothing about storage, uplinks or the clock: they deliver
// SensorReadings, the manager turns them into Measurements.

// Static description of a sensor model: the "sensor metadata registry".
struct PhenomenonSpec {
  Phenomenon phenomenon;
  Unit unit;
  float min;         // physical measurement range of the sensor
  float max;
  float accuracy;    // typical accuracy (same unit)
  float resolution;
};

struct SensorDescriptor {
  const char* type;           // registry key, e.g. "am2315c"
  const char* manufacturer;
  const char* model;
  const PhenomenonSpec* phenomena;
  uint8_t phenomenonCount;
  uint8_t i2cAddress;         // 0 = not an I2C sensor
  uint32_t initTimeoutMs;     // max time from begin()/reset() to Ready (including warm-up)
  uint32_t measureTimeoutMs;  // max time from requestMeasurement() to results
  uint32_t minIntervalMs;
  bool outdoorSuitable;
  const char* notes;          // mounting / usage hints

  const PhenomenonSpec* spec(Phenomenon p) const {
    for (uint8_t i = 0; i < phenomenonCount; ++i) {
      if (phenomena[i].phenomenon == p) return &phenomena[i];
    }
    return nullptr;
  }
};

enum class SensorState : uint8_t { Uninitialized = 0, Initializing, WarmingUp, Ready, Measuring, Error, Disabled };

enum class SensorError : uint8_t {
  None = 0,
  NotFound,     // no answer at init
  CommError,    // bus error / NACK
  Timeout,      // no result in time
  CrcError,
  DeviceFault,  // sensor reports an internal fault (e.g. SPS30 fan/laser)
  InvalidData,
  NoDriver,     // configured type unknown to this firmware
};

const char* sensorStateName(SensorState s);
const char* sensorErrorName(SensorError e);

struct SensorReading {
  Phenomenon phenomenon;
  float value;
  uint16_t quality;  // driver-side flags, e.g. quality::kWarmup
};

class Sensor {
 public:
  static constexpr uint8_t kMaxReadings = 8;

  virtual ~Sensor() = default;

  virtual const SensorDescriptor& descriptor() const = 0;
  // Starts initialisation. Must return quickly; progress happens in tick().
  virtual void begin(const config::SensorConfig& cfg, uint32_t nowMs) = 0;
  virtual void tick(uint32_t nowMs) = 0;
  // Starts a measurement. Returns false if the sensor is not Ready.
  virtual bool requestMeasurement(uint32_t nowMs) = 0;
  // Moves finished readings into `out`. Returns the number of readings (0 if none yet).
  virtual uint8_t takeResults(SensorReading* out, uint8_t max) = 0;
  virtual SensorState state() const = 0;
  virtual SensorError lastError() const = 0;
  // Re-initialises after an error (bus recovery, soft reset, ...). Same contract as begin().
  virtual void reset(uint32_t nowMs) = 0;
  // Optional: serial number for traceability.
  virtual bool serialNumber(char* buf, size_t len) const {
    (void)buf;
    (void)len;
    return false;
  }
};

// Registry entry for one driver type. The table lives with the drivers (device code);
// the SensorManager only gets a pointer to it, so it can be tested with fake drivers.
struct DriverEntry {
  const char* type;
  const SensorDescriptor* descriptor;
  Sensor* (*create)();  // called at setup only
};

const DriverEntry* findDriver(const DriverEntry* table, size_t count, const char* type);
