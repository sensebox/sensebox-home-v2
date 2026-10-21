#include "QualityChecker.h"

#include <math.h>
#include <stdio.h>

namespace {

constexpr PlausibilityLimits kNoLimits = {-1e9f, 1e9f, 0.0f, 0, -1e9f, 1e9f};

// min, max, maxDeltaPerMin, stuckCount, saturatedBelow, saturatedAbove
constexpr PlausibilityLimits kTemperature = {-40.0f, 60.0f, 5.0f, 30, -40.0f, 60.0f};
// Rain and fog keep RH at the top for hours.
constexpr PlausibilityLimits kHumidity = {0.0f, 100.0f, 20.0f, 30, 0.5f, 98.0f};
// PM can legitimately jump (traffic, fireworks), so no spike check.
constexpr PlausibilityLimits kParticulates = {0.0f, 1000.0f, 0.0f, 30, 0.5f, 1000.0f};

void applyOverride(const config::SensorConfig& sensor, const char* prefix, const char* field, float& value) {
  char key[config::kParamKeyLen];
  snprintf(key, sizeof(key), "%s.%s", prefix, field);
  value = sensor.param(key, value);
}

}  // namespace

const PlausibilityLimits& defaultLimits(Phenomenon p) {
  switch (p) {
    case Phenomenon::AirTemperature: return kTemperature;
    case Phenomenon::RelativeHumidity: return kHumidity;
    case Phenomenon::PM1:
    case Phenomenon::PM2_5:
    case Phenomenon::PM4:
    case Phenomenon::PM10: return kParticulates;
    default: return kNoLimits;
  }
}

PlausibilityLimits resolveLimits(Phenomenon p, const config::SensorConfig* sensor) {
  PlausibilityLimits lim = defaultLimits(p);
  if (sensor == nullptr || sensor->paramCount == 0) return lim;

  const char* prefix = phenomenonShortName(p);
  applyOverride(*sensor, prefix, "min", lim.min);
  applyOverride(*sensor, prefix, "max", lim.max);
  applyOverride(*sensor, prefix, "maxDelta", lim.maxDeltaPerMin);
  applyOverride(*sensor, prefix, "saturatedBelow", lim.saturatedBelow);
  applyOverride(*sensor, prefix, "saturatedAbove", lim.saturatedAbove);
  float stuck = lim.stuckCount;
  applyOverride(*sensor, prefix, "stuckCount", stuck);
  lim.stuckCount = stuck <= 0.0f ? 0 : stuck >= 255.0f ? 255 : static_cast<uint8_t>(stuck);
  return lim;
}

void QualityChecker::reset() {
  for (auto& row : history_) {
    for (auto& h : row) h = History{};
  }
}

uint16_t QualityChecker::check(const Measurement& m) {
  const uint8_t p = static_cast<uint8_t>(m.phenomenon);
  if (m.sensorSlot >= config::kMaxSensors || p >= kPhenomena) return 0;

  const PlausibilityLimits lim = resolveLimits(m.phenomenon, cfg_ != nullptr ? cfg_->sensorBySlot(m.sensorSlot) : nullptr);
  History& h = history_[m.sensorSlot][p];
  uint16_t flags = 0;

  if (isnan(m.value) || m.value < lim.min || m.value > lim.max) flags |= quality::kImplausible;

  if (h.hasLast && !isnan(m.value)) {
    if (lim.maxDeltaPerMin > 0.0f) {
      // At least one minute, so that closely spaced samples don't inflate the rate.
      const uint32_t dt = m.timestamp > h.lastTimestamp ? m.timestamp - h.lastTimestamp : 0;
      const float minutes = dt < 60 ? 1.0f : static_cast<float>(dt) / 60.0f;
      if (fabsf(m.value - h.last) / minutes > lim.maxDeltaPerMin) flags |= quality::kSpike;
    }
    const bool saturated = m.value <= lim.saturatedBelow || m.value >= lim.saturatedAbove;
    if (m.value == h.last && !saturated) {
      if (h.sameCount < 255) ++h.sameCount;
    } else {
      h.sameCount = 0;
    }
    if (lim.stuckCount > 0 && h.sameCount + 1 >= lim.stuckCount) flags |= quality::kStuck;
  }

  if (!isnan(m.value)) {
    h.hasLast = true;
    h.last = m.value;
    h.lastTimestamp = m.timestamp;
  }
  return flags;
}
