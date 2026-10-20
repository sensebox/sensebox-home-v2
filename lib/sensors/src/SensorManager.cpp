#include "SensorManager.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Log.h"
#include "Scheduling.h"

namespace {
constexpr const char* kTag = "sensors";
}

SensorManager::SensorManager(Clock& clock, HealthRegistry& health, MeasurementSink& sink)
    : SensorManager(clock, health, sink, Options{}) {}

SensorManager::SensorManager(Clock& clock, HealthRegistry& health, MeasurementSink& sink, Options options)
    : clock_(clock), health_(health), sink_(sink), options_(options) {}

SensorManager::~SensorManager() { releaseSensors(); }

void SensorManager::releaseSensors() {
  for (size_t i = 0; i < slotCount_; ++i) {
    delete slots_[i].sensor;
    slots_[i] = Slot{};
  }
  slotCount_ = 0;
}

void SensorManager::configure(const config::StationConfig& cfg, const DriverEntry* drivers, size_t driverCount) {
  releaseSensors();
  for (uint8_t i = 0; i < cfg.sensorCount && slotCount_ < config::kMaxSensors; ++i) {
    Slot& s = slots_[slotCount_++];
    s.cfg = cfg.sensors[i];

    char name[ComponentHealth::kNameLen];
    snprintf(name, sizeof(name), "sensor%u:%.12s", s.cfg.slot, s.cfg.type);
    s.healthId = health_.add(name);

    if (!s.cfg.enabled) {
      s.phase = Phase::Disabled;
      health_.setStatus(s.healthId, ComponentStatus::Disabled);
      continue;
    }
    const DriverEntry* entry = findDriver(drivers, driverCount, s.cfg.type);
    if (entry == nullptr) {
      s.phase = Phase::NoDriver;
      s.lastError = SensorError::NoDriver;
      ++s.errorCount;
      health_.reportError(s.healthId, static_cast<int32_t>(SensorError::NoDriver), "unknown sensor type");
      LOGE(kTag, "slot %u: no driver for type '%s'", s.cfg.slot, s.cfg.type);
      continue;
    }
    s.sensor = entry->create();
  }
}

void SensorManager::begin(uint32_t nowMs) {
  for (size_t i = 0; i < slotCount_; ++i) {
    Slot& s = slots_[i];
    if (s.sensor == nullptr) continue;
    LOGI(kTag, "slot %u: starting %s", s.cfg.slot, s.cfg.type);
    s.sensor->begin(s.cfg, nowMs);
    enterPhase(s, Phase::Starting, nowMs);
  }
}

void SensorManager::tick(uint32_t nowMs) {
  for (size_t i = 0; i < slotCount_; ++i) {
    if (slots_[i].sensor != nullptr) tickSlot(slots_[i], nowMs);
  }
}

void SensorManager::enterPhase(Slot& s, Phase phase, uint32_t nowMs) {
  s.phase = phase;
  s.phaseStartMs = nowMs;
}

void SensorManager::tickSlot(Slot& s, uint32_t nowMs) {
  Sensor& sensor = *s.sensor;
  if (s.phase != Phase::WaitingRetry) sensor.tick(nowMs);
  const SensorState st = sensor.state();
  const SensorDescriptor& d = sensor.descriptor();

  switch (s.phase) {
    case Phase::Starting:
      if (st == SensorState::Error) {
        handleError(s, sensor.lastError(), nowMs);
      } else if (st == SensorState::Ready) {
        LOGI(kTag, "slot %u: %s ready", s.cfg.slot, s.cfg.type);
        enterPhase(s, Phase::Running, nowMs);
        if (onGrid(s)) {
          scheduleOnGrid(s, nowMs);
        } else {
          s.nextMeasureMs = nowMs;
        }
      } else if (nowMs - s.phaseStartMs > d.initTimeoutMs) {
        handleError(s, SensorError::Timeout, nowMs);
      }
      break;

    case Phase::Running:
      // First sync (or a resync): move the schedule onto the new grid.
      if (onGrid(s) && s.gridSync != clock_.syncCount()) scheduleOnGrid(s, nowMs);
      if (st == SensorState::Error) {
        handleError(s, sensor.lastError(), nowMs);
      } else if (st == SensorState::Ready && timeReached(nowMs, s.nextMeasureMs)) {
        if (sensor.requestMeasurement(nowMs)) {
          enterPhase(s, Phase::Measuring, nowMs);
          if (onGrid(s)) {
            scheduleOnGrid(s, nowMs);
          } else {
            const uint32_t intervalMs = s.cfg.intervalSec * 1000u;
            s.nextMeasureMs += intervalMs;
            // Don't try to catch up missed measurements after a long outage.
            if (timeReached(nowMs, s.nextMeasureMs)) s.nextMeasureMs = nowMs + intervalMs;
          }
          // A driver that measures synchronously has its results available right away.
          const uint8_t n = sensor.takeResults(readings_, Sensor::kMaxReadings);
          if (n > 0) emitResults(s, n);
        }
      }
      break;

    case Phase::Measuring: {
      const uint8_t n = sensor.takeResults(readings_, Sensor::kMaxReadings);
      if (n > 0) {
        emitResults(s, n);
      } else if (st == SensorState::Error) {
        handleError(s, sensor.lastError(), nowMs);
      } else if (nowMs - s.phaseStartMs > d.measureTimeoutMs) {
        handleError(s, SensorError::Timeout, nowMs);
      }
      break;
    }

    case Phase::WaitingRetry:
      if (timeReached(nowMs, s.retryAtMs)) {
        LOGI(kTag, "slot %u: retrying %s (attempt %u)", s.cfg.slot, s.cfg.type, s.retryAttempt);
        sensor.reset(nowMs);
        enterPhase(s, Phase::Starting, nowMs);
      }
      break;

    case Phase::Disabled:
    case Phase::NoDriver:
      break;
  }
}

// With a synced clock and an interval that divides a day, measurements follow the UTC grid
// (60 s -> every full minute), so window means cover the same samples on every box.
// Before the first sync they run relative to the sensor start.
bool SensorManager::onGrid(const Slot& s) const { return clock_.isSynced() && alignsToDay(s.cfg.intervalSec); }

void SensorManager::scheduleOnGrid(Slot& s, uint32_t nowMs) {
  s.nextMeasureMs = nowMs + msToNextMultiple(clock_.nowUtcMs(), s.cfg.intervalSec * 1000u);
  s.gridSync = clock_.syncCount();
}

void SensorManager::handleError(Slot& s, SensorError error, uint32_t nowMs) {
  if (error == SensorError::None) error = SensorError::CommError;
  ++s.errorCount;
  s.lastError = error;
  const uint32_t delay = backoffMs(options_.retryBaseMs, options_.retryMaxMs, s.retryAttempt);
  if (s.retryAttempt < 255) ++s.retryAttempt;
  s.retryAtMs = nowMs + delay;
  enterPhase(s, Phase::WaitingRetry, nowMs);

  char msg[ComponentHealth::kErrorLen];
  snprintf(msg, sizeof(msg), "%s, retry in %lu s", sensorErrorName(error), static_cast<unsigned long>(delay / 1000));
  health_.reportError(s.healthId, static_cast<int32_t>(error), msg);
  LOGW(kTag, "slot %u: %s %s (errors: %lu)", s.cfg.slot, s.cfg.type, msg, static_cast<unsigned long>(s.errorCount));
}

void SensorManager::emitResults(Slot& s, uint8_t count) {
  const SensorDescriptor& d = s.sensor->descriptor();
  uint16_t clockQuality = 0;
  const uint32_t timestamp = clock_.timestampNow(clockQuality);

  for (uint8_t i = 0; i < count; ++i) {
    const SensorReading& r = readings_[i];
    const PhenomenonSpec* spec = d.spec(r.phenomenon);
    if (spec == nullptr) continue;
    Measurement m{};
    m.timestamp = timestamp;
    m.value = r.value;
    m.sensorSlot = s.cfg.slot;
    m.phenomenon = r.phenomenon;
    m.unit = spec->unit;
    m.kind = ValueKind::Raw;
    m.quality = static_cast<uint16_t>(r.quality | clockQuality);
    if (isnan(r.value) || r.value < spec->min || r.value > spec->max) m.quality |= quality::kOutOfRange;
    sink_.onMeasurement(m);
  }

  if (s.retryAttempt > 0) LOGI(kTag, "slot %u: %s recovered", s.cfg.slot, s.cfg.type);
  s.retryAttempt = 0;
  s.lastGoodUptimeS = clock_.uptimeS();
  ++s.readingCount;
  health_.reportSuccess(s.healthId, clock_.nowUtc());
  enterPhase(s, Phase::Running, s.phaseStartMs);
}

bool SensorManager::status(size_t index, uint32_t nowMs, SensorSlotStatus& out) const {
  if (index >= slotCount_) return false;
  const Slot& s = slots_[index];
  out = SensorSlotStatus{};
  out.slot = s.cfg.slot;
  strncpy(out.type, s.cfg.type, sizeof(out.type) - 1);
  switch (s.phase) {
    case Phase::Disabled: out.state = SensorState::Disabled; break;
    case Phase::NoDriver:
    case Phase::WaitingRetry: out.state = SensorState::Error; break;
    default: out.state = s.sensor != nullptr ? s.sensor->state() : SensorState::Error; break;
  }
  out.lastError = s.lastError;
  out.errorCount = s.errorCount;
  out.retryInMs = s.phase == Phase::WaitingRetry ? timeUntil(nowMs, s.retryAtMs) : 0;
  out.lastGoodUptimeS = s.lastGoodUptimeS;
  out.readingCount = s.readingCount;
  return true;
}
