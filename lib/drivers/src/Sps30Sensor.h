#pragma once

#include <SensirionI2cSps30.h>

#include "Sensor.h"

// Sensirion SPS30 particulate matter sensor (I2C 0x69), plan §6 "SPS30 operation".
// Runs in continuous measurement mode; after start it needs 30 s before values are valid.

extern const SensorDescriptor kSps30Descriptor;

class Sps30Sensor : public Sensor {
 public:
  const SensorDescriptor& descriptor() const override { return kSps30Descriptor; }
  void begin(const config::SensorConfig& cfg, uint32_t nowMs) override;
  void tick(uint32_t nowMs) override;
  bool requestMeasurement(uint32_t nowMs) override;
  uint8_t takeResults(SensorReading* out, uint8_t max) override;
  SensorState state() const override { return state_; }
  SensorError lastError() const override { return error_; }
  void reset(uint32_t nowMs) override;
  bool serialNumber(char* buf, size_t len) const override;

 private:
  void start(uint32_t nowMs);
  void fail(SensorError error, int16_t code, const char* what);
  void pollMeasurement(uint32_t nowMs);
  bool checkDeviceStatus();

  SensirionI2cSps30 sps_;
  SensorState state_ = SensorState::Uninitialized;
  SensorError error_ = SensorError::None;
  uint32_t fanCleaningDays_ = 4;
  uint32_t warmupUntilMs_ = 0;
  uint32_t lastPollMs_ = 0;
  uint32_t measurements_ = 0;
  bool hasResult_ = false;
  float pm1_ = 0.0f;
  float pm2_5_ = 0.0f;
  float pm4_ = 0.0f;
  float pm10_ = 0.0f;
  char serial_[33] = {};
};
