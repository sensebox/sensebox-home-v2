#pragma once

#include <Adafruit_AHTX0.h>

#include "Sensor.h"

// Adafruit AM2315C encased temperature/humidity probe (AHT20 inside, I2C 0x38), plan §6.
// Uses the Adafruit AHTX0 library.

extern const SensorDescriptor kAm2315cDescriptor;

class Am2315cSensor : public Sensor {
 public:
  const SensorDescriptor& descriptor() const override { return kAm2315cDescriptor; }
  void begin(const config::SensorConfig& cfg, uint32_t nowMs) override;
  void tick(uint32_t nowMs) override;
  bool requestMeasurement(uint32_t nowMs) override;
  uint8_t takeResults(SensorReading* out, uint8_t max) override;
  SensorState state() const override { return state_; }
  SensorError lastError() const override { return error_; }
  void reset(uint32_t nowMs) override;

 private:
  void fail(SensorError error);

  Adafruit_AHTX0 aht_;
  SensorState state_ = SensorState::Uninitialized;
  SensorError error_ = SensorError::None;
  bool hasResult_ = false;
  float temperature_ = 0.0f;
  float humidity_ = 0.0f;
};
