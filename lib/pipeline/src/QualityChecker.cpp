#include "QualityChecker.h"

#include <math.h>

namespace {

constexpr PlausibilityLimits kNoLimits = {-1e9f, 1e9f, 0.0f, 0};

// min, max, maxDeltaPerMin, stuckCount
constexpr PlausibilityLimits kTemperature = {-40.0f, 60.0f, 5.0f, 30};
constexpr PlausibilityLimits kHumidity = {0.0f, 100.0f, 20.0f, 30};
// PM can legitimately jump (traffic, fireworks), so no spike check.
constexpr PlausibilityLimits kParticulates = {0.0f, 1000.0f, 0.0f, 30};

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

void QualityChecker::reset() {
  for (auto& row : history_) {
    for (auto& h : row) h = History{};
  }
}

uint16_t QualityChecker::check(const Measurement& m) {
  const uint8_t p = static_cast<uint8_t>(m.phenomenon);
  if (m.sensorSlot >= config::kMaxSensors || p >= kPhenomena) return 0;

  const PlausibilityLimits& lim = defaultLimits(m.phenomenon);
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
    // A sensor sitting at the edge of its range (0 µg/m³, 100 % RH) is saturated, not stuck.
    const bool atLimit = m.value <= lim.min || m.value >= lim.max;
    if (m.value == h.last && !atLimit) {
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
