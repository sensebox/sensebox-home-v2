#include "Sps30Sensor.h"

#include <Wire.h>
#include <string.h>

#include "Board.h"
#include "Log.h"
#include "Scheduling.h"

namespace {
constexpr const char* kTag = "sps30";
constexpr uint32_t kWarmupMs = 30000;
constexpr uint32_t kPollIntervalMs = 200;
// Read the device status register every N measurements (fan / laser / speed warnings).
constexpr uint32_t kStatusCheckEvery = 10;
constexpr uint32_t kStatusSpeedWarning = 1u << 21;
constexpr uint32_t kStatusLaserError = 1u << 5;
constexpr uint32_t kStatusFanError = 1u << 4;

constexpr PhenomenonSpec kSpecs[] = {
    {Phenomenon::PM1, Unit::UgPerM3, 0.0f, 1000.0f, 10.0f, 0.1f},
    {Phenomenon::PM2_5, Unit::UgPerM3, 0.0f, 1000.0f, 10.0f, 0.1f},
    {Phenomenon::PM4, Unit::UgPerM3, 0.0f, 1000.0f, 25.0f, 0.1f},
    {Phenomenon::PM10, Unit::UgPerM3, 0.0f, 1000.0f, 25.0f, 0.1f},
};
}  // namespace

const SensorDescriptor kSps30Descriptor = {
    "sps30",
    "Sensirion",
    "SPS30",
    kSpecs,
    4,
    SPS30_I2C_ADDR_69,
    kWarmupMs + 10000,  // init timeout incl. warm-up
    3000,               // measure timeout (new data every second)
    1000,               // min interval
    true,
    "5 V via adapter board, I2C max 100 kHz. Inlet must not face up; protect from rain.",
};

void Sps30Sensor::fail(SensorError error, int16_t code, const char* what) {
  state_ = SensorState::Error;
  error_ = error;
  hasResult_ = false;
  LOGW(kTag, "%s failed (code %d)", what, code);
}

void Sps30Sensor::begin(const config::SensorConfig& cfg, uint32_t nowMs) {
  fanCleaningDays_ = static_cast<uint32_t>(cfg.param("fanCleaningIntervalDays", 4.0f));
  sps_.begin(Wire, SPS30_I2C_ADDR_69);
  start(nowMs);
}

void Sps30Sensor::reset(uint32_t nowMs) {
  if (board::sensorBusStuck()) board::recoverSensorBus();
  start(nowMs);
}

// NOTE: blocks ~150 ms in total (library delays after reset and commands); only at (re)start.
void Sps30Sensor::start(uint32_t nowMs) {
  state_ = SensorState::Initializing;
  error_ = SensorError::None;
  hasResult_ = false;

  // A stop first, in case the sensor is still measuring from before a reboot of the MCU.
  sps_.stopMeasurement();
  int16_t err = sps_.deviceReset();
  if (err != 0) return fail(SensorError::NotFound, err, "reset");

  int8_t serial[sizeof(serial_)] = {};
  err = sps_.readSerialNumber(serial, sizeof(serial));
  if (err != 0) return fail(SensorError::CommError, err, "read serial");
  memcpy(serial_, serial, sizeof(serial_) - 1);
  serial_[sizeof(serial_) - 1] = '\0';

  if (fanCleaningDays_ > 0) {
    err = sps_.writeAutoCleaningInterval(fanCleaningDays_ * 86400u);
    if (err != 0) return fail(SensorError::CommError, err, "set fan cleaning");
  }
  err = sps_.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
  if (err != 0) return fail(SensorError::CommError, err, "start measurement");

  warmupUntilMs_ = nowMs + kWarmupMs;
  state_ = SensorState::WarmingUp;
  LOGI(kTag, "started, serial %s, fan cleaning every %lu days, warming up", serial_,
       static_cast<unsigned long>(fanCleaningDays_));
}

void Sps30Sensor::tick(uint32_t nowMs) {
  if (state_ == SensorState::WarmingUp && timeReached(nowMs, warmupUntilMs_)) {
    state_ = SensorState::Ready;
  } else if (state_ == SensorState::Measuring && nowMs - lastPollMs_ >= kPollIntervalMs) {
    pollMeasurement(nowMs);
  }
}

bool Sps30Sensor::requestMeasurement(uint32_t nowMs) {
  if (state_ != SensorState::Ready) return false;
  state_ = SensorState::Measuring;
  lastPollMs_ = nowMs - kPollIntervalMs;  // poll on the next tick
  return true;
}

void Sps30Sensor::pollMeasurement(uint32_t nowMs) {
  lastPollMs_ = nowMs;
  uint16_t ready = 0;
  int16_t err = sps_.readDataReadyFlag(ready);
  if (err != 0) return fail(SensorError::CommError, err, "data ready");
  if (!ready) return;  // the manager's measure timeout catches a sensor that never gets ready

  float nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalSize;
  err = sps_.readMeasurementValuesFloat(pm1_, pm2_5_, pm4_, pm10_, nc0p5, nc1p0, nc2p5, nc4p0, nc10p0, typicalSize);
  if (err != 0) return fail(SensorError::CommError, err, "read values");

  ++measurements_;
  if (measurements_ % kStatusCheckEvery == 1 && !checkDeviceStatus()) return;

  hasResult_ = true;
  state_ = SensorState::Ready;
  LOGD(kTag, "PM1=%.1f PM2.5=%.1f PM4=%.1f PM10=%.1f", pm1_, pm2_5_, pm4_, pm10_);
}

bool Sps30Sensor::checkDeviceStatus() {
  uint32_t status = 0;
  const int16_t err = sps_.readDeviceStatusRegister(status);
  if (err != 0) {
    fail(SensorError::CommError, err, "read status");
    return false;
  }
  if (status & (kStatusFanError | kStatusLaserError)) {
    LOGE(kTag, "device fault: status 0x%08lx (%s%s)", static_cast<unsigned long>(status),
         (status & kStatusFanError) ? "fan " : "", (status & kStatusLaserError) ? "laser" : "");
    sps_.clearDeviceStatusRegister();
    state_ = SensorState::Error;
    error_ = SensorError::DeviceFault;
    hasResult_ = false;
    return false;
  }
  if (status & kStatusSpeedWarning) LOGW(kTag, "fan speed out of range (status 0x%08lx)", static_cast<unsigned long>(status));
  return true;
}

uint8_t Sps30Sensor::takeResults(SensorReading* out, uint8_t max) {
  if (!hasResult_ || max < 4) return 0;
  hasResult_ = false;
  out[0] = {Phenomenon::PM1, pm1_, 0};
  out[1] = {Phenomenon::PM2_5, pm2_5_, 0};
  out[2] = {Phenomenon::PM4, pm4_, 0};
  out[3] = {Phenomenon::PM10, pm10_, 0};
  return 4;
}

bool Sps30Sensor::serialNumber(char* buf, size_t len) const {
  if (serial_[0] == '\0' || len == 0) return false;
  strncpy(buf, serial_, len - 1);
  buf[len - 1] = '\0';
  return true;
}
