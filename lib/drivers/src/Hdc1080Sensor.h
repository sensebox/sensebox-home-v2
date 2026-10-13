#pragma once

#include <Adafruit_HDC1000.h>

#include "Sensor.h"

// TI HDC1080 temperature/humidity sensor (I2C 0x40), e.g. the senseBox HDC1080 board.
// Uses the Adafruit HDC1000 library. Not used in the Olpe stations (they use the AM2315C);
// supported for existing boxes and for bench testing.

extern const SensorDescriptor kHdc1080Descriptor;

class Hdc1080Sensor : public Sensor {
 public:
  const SensorDescriptor& descriptor() const override { return kHdc1080Descriptor; }
  void begin(const config::SensorConfig& cfg, uint32_t nowMs) override;
  void tick(uint32_t nowMs) override;
  bool requestMeasurement(uint32_t nowMs) override;
  uint8_t takeResults(SensorReading* out, uint8_t max) override;
  SensorState state() const override { return state_; }
  SensorError lastError() const override { return error_; }
  void reset(uint32_t nowMs) override;

 private:
  void fail(SensorError error);

  Adafruit_HDC1000 hdc_;
  SensorState state_ = SensorState::Uninitialized;
  SensorError error_ = SensorError::None;
  bool hasResult_ = false;
  float temperature_ = 0.0f;
  float humidity_ = 0.0f;
};
