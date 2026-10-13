#include "Am2315cSensor.h"

#include <Wire.h>

#include "Board.h"
#include "Log.h"

namespace {
constexpr const char* kTag = "am2315c";
constexpr uint8_t kAddress = AHTX0_I2CADDR_DEFAULT;  // 0x38

constexpr PhenomenonSpec kSpecs[] = {
    {Phenomenon::AirTemperature, Unit::DegC, -40.0f, 80.0f, 0.3f, 0.01f},
    {Phenomenon::RelativeHumidity, Unit::Percent, 0.0f, 100.0f, 2.0f, 0.024f},
};
}  // namespace

const SensorDescriptor kAm2315cDescriptor = {
    "am2315c",
    "Adafruit (Aosong)",
    "AM2315C / AHT20",
    kSpecs,
    2,
    kAddress,
    2000,  // init timeout
    1000,  // measure timeout
    2000,  // min interval (self-heating)
    true,
    "Encased probe on 50 cm cable; mount inside the radiation shield. Not rated weatherproof.",
};

void Am2315cSensor::fail(SensorError error) {
  state_ = SensorState::Error;
  error_ = error;
  hasResult_ = false;
}

void Am2315cSensor::begin(const config::SensorConfig& cfg, uint32_t nowMs) {
  (void)cfg;
  (void)nowMs;
  state_ = SensorState::Initializing;
  error_ = SensorError::None;
  hasResult_ = false;
  // NOTE: begin() blocks ~40-60 ms (power-up wait, soft reset, calibration).
  if (!aht_.begin(&Wire, 0, kAddress)) {
    fail(SensorError::NotFound);
    return;
  }
  state_ = SensorState::Ready;
}

void Am2315cSensor::reset(uint32_t nowMs) {
  if (board::sensorBusStuck()) board::recoverSensorBus();
  begin(config::SensorConfig{}, nowMs);
}

void Am2315cSensor::tick(uint32_t nowMs) { (void)nowMs; }

bool Am2315cSensor::requestMeasurement(uint32_t nowMs) {
  (void)nowMs;
  if (state_ != SensorState::Ready) return false;

  // The library's busy-wait in getEvent() has no upper bound: a failed status read returns 0xFF,
  // which looks like "busy" forever. Check that the sensor answers before starting, so an
  // unplugged probe becomes a normal error instead of a watchdog reset.
  const uint8_t status = aht_.getStatus();
  if (status == 0xFF) {
    fail(SensorError::CommError);
    return true;
  }

  state_ = SensorState::Measuring;
  sensors_event_t humidity;
  sensors_event_t temperature;
  // NOTE: Adafruit_AHTX0::getEvent() blocks ~80 ms while the AHT20 measures.
  // Accepted: bounded wait, once per interval. Do not call it more often than needed.
  if (!aht_.getEvent(&humidity, &temperature)) {
    fail(SensorError::CommError);
    return true;
  }
  temperature_ = temperature.temperature;
  humidity_ = humidity.relative_humidity;
  hasResult_ = true;
  state_ = SensorState::Ready;
  LOGD(kTag, "T=%.2f RH=%.2f", temperature_, humidity_);
  return true;
}

uint8_t Am2315cSensor::takeResults(SensorReading* out, uint8_t max) {
  if (!hasResult_ || max < 2) return 0;
  hasResult_ = false;
  out[0] = {Phenomenon::AirTemperature, temperature_, 0};
  out[1] = {Phenomenon::RelativeHumidity, humidity_, 0};
  return 2;
}
