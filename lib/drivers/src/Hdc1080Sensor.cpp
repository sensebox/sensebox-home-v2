#include "Hdc1080Sensor.h"

#include <Wire.h>

#include "Board.h"
#include "Log.h"

namespace {
constexpr const char* kTag = "hdc1080";
constexpr uint8_t kAddress = HDC1000_I2CADDR;  // 0x40

constexpr PhenomenonSpec kSpecs[] = {
    {Phenomenon::AirTemperature, Unit::DegC, -40.0f, 125.0f, 0.2f, 0.01f},
    {Phenomenon::RelativeHumidity, Unit::Percent, 0.0f, 100.0f, 2.0f, 0.01f},
};

// The library ignores I2C read errors, so check that the sensor answers.
bool present() {
  Wire.beginTransmission(kAddress);
  return Wire.endTransmission() == 0;
}
}  // namespace

const SensorDescriptor kHdc1080Descriptor = {
    "hdc1080",
    "Texas Instruments",
    "HDC1080",
    kSpecs,
    2,
    kAddress,
    1000,  // init timeout
    500,   // measure timeout
    1000,  // min interval
    true,
    "Board sensor; needs a radiation shield outdoors. Known to read high when mounted close to electronics.",
};

void Hdc1080Sensor::fail(SensorError error) {
  state_ = SensorState::Error;
  error_ = error;
  hasResult_ = false;
}

void Hdc1080Sensor::begin(const config::SensorConfig& cfg, uint32_t nowMs) {
  (void)cfg;
  (void)nowMs;
  state_ = SensorState::Initializing;
  error_ = SensorError::None;
  hasResult_ = false;
  // NOTE: begin() blocks ~15 ms (config write after reset).
  if (!hdc_.begin(kAddress, &Wire)) {
    fail(SensorError::NotFound);
    return;
  }
  state_ = SensorState::Ready;
}

void Hdc1080Sensor::reset(uint32_t nowMs) {
  if (board::sensorBusStuck()) board::recoverSensorBus();
  begin(config::SensorConfig{}, nowMs);
}

void Hdc1080Sensor::tick(uint32_t nowMs) { (void)nowMs; }

bool Hdc1080Sensor::requestMeasurement(uint32_t nowMs) {
  (void)nowMs;
  if (state_ != SensorState::Ready) return false;
  if (!present()) {
    fail(SensorError::CommError);
    return true;
  }
  state_ = SensorState::Measuring;
  // NOTE: each read blocks ~20 ms (conversion time); bounded, once per interval.
  const float temperature = hdc_.readTemperature();
  const float humidity = hdc_.readHumidity();
  if (!present()) {
    fail(SensorError::CommError);
    return true;
  }
  // All-zero raw data (= -40 °C and 0 %) means the read returned nothing.
  if (temperature <= -40.0f && humidity <= 0.0f) {
    fail(SensorError::InvalidData);
    return true;
  }
  temperature_ = temperature;
  humidity_ = humidity;
  hasResult_ = true;
  state_ = SensorState::Ready;
  LOGD(kTag, "T=%.2f RH=%.2f", temperature_, humidity_);
  return true;
}

uint8_t Hdc1080Sensor::takeResults(SensorReading* out, uint8_t max) {
  if (!hasResult_ || max < 2) return 0;
  hasResult_ = false;
  out[0] = {Phenomenon::AirTemperature, temperature_, 0};
  out[1] = {Phenomenon::RelativeHumidity, humidity_, 0};
  return 2;
}
