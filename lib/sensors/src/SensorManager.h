#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Clock.h"
#include "Health.h"
#include "MeasurementSink.h"
#include "Sensor.h"
#include "StationConfig.h"

// Owns the configured sensor instances and runs them (plan §4.3):
// scheduling per interval, init/measure timeouts, error counting, retry with exponential
// backoff, health reporting. A failing sensor never affects the others.

struct SensorSlotStatus {
  uint8_t slot;
  char type[config::kTypeLen];
  SensorState state;
  SensorError lastError;
  uint32_t errorCount;
  uint32_t retryInMs;        // 0 if not waiting for a retry
  uint32_t lastGoodUptimeS;  // 0 = never
  uint32_t readingCount;     // successful measurements since boot
};

class SensorManager {
 public:
  struct Options {
    uint32_t retryBaseMs = 10000;
    uint32_t retryMaxMs = 10u * 60u * 1000u;
  };

  SensorManager(Clock& clock, HealthRegistry& health, MeasurementSink& sink);
  SensorManager(Clock& clock, HealthRegistry& health, MeasurementSink& sink, Options options);
  ~SensorManager();
  SensorManager(const SensorManager&) = delete;
  SensorManager& operator=(const SensorManager&) = delete;

  // Creates sensor instances for the configured sensors. Call at setup only (allocates).
  // Unknown types get a slot in state Error/NoDriver so they show up in the UI.
  void configure(const config::StationConfig& cfg, const DriverEntry* drivers, size_t driverCount);
  void begin(uint32_t nowMs);
  void tick(uint32_t nowMs);

  size_t slotCount() const { return slotCount_; }
  bool status(size_t index, uint32_t nowMs, SensorSlotStatus& out) const;
  // Driver of a slot (e.g. for serial number / descriptor). nullptr if none.
  const Sensor* sensor(size_t index) const { return index < slotCount_ ? slots_[index].sensor : nullptr; }

 private:
  enum class Phase : uint8_t { Disabled, NoDriver, Starting, Running, Measuring, WaitingRetry };

  struct Slot {
    Sensor* sensor = nullptr;
    config::SensorConfig cfg;
    HealthRegistry::Id healthId = HealthRegistry::kInvalid;
    Phase phase = Phase::Disabled;
    uint32_t phaseStartMs = 0;
    uint32_t nextMeasureMs = 0;
    uint32_t retryAtMs = 0;
    uint8_t retryAttempt = 0;
    uint32_t errorCount = 0;
    SensorError lastError = SensorError::None;
    uint32_t lastGoodUptimeS = 0;
    uint32_t readingCount = 0;
  };

  void tickSlot(Slot& s, uint32_t nowMs);
  void enterPhase(Slot& s, Phase phase, uint32_t nowMs);
  void handleError(Slot& s, SensorError error, uint32_t nowMs);
  void emitResults(Slot& s, uint8_t count);
  void releaseSensors();

  Clock& clock_;
  HealthRegistry& health_;
  MeasurementSink& sink_;
  Options options_;
  Slot slots_[config::kMaxSensors];
  size_t slotCount_ = 0;
  SensorReading readings_[Sensor::kMaxReadings];
};
