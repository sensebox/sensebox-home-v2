#pragma once

#include <stdint.h>

#include "Measurement.h"
#include "StationConfig.h"

// Plausibility, spike and stuck-value checks (plan §4.4). Values are flagged, never dropped.
// The sensor's physical range (quality::kOutOfRange) is checked by the SensorManager, which
// knows the sensor descriptor.
// Limits are defaults per phenomenon; each sensor can override them with params named
// "<short name>.<field>", e.g. "rh.stuckCount": 0 (see resolveLimits()).

struct PlausibilityLimits {
  float min;              // plausible environmental range
  float max;
  float maxDeltaPerMin;   // spike threshold; 0 = no spike check
  uint8_t stuckCount;     // identical consecutive values that count as stuck; 0 = no check
  // Values at or beyond these are saturation (100 % RH in fog, 0 µg/m³ in clean air) and never
  // count as stuck. Drivers rarely hit the exact range limit (HDC1080 tops out at 99.998 %).
  float saturatedBelow;
  float saturatedAbove;
};

// Defaults for outdoor air measurements.
const PlausibilityLimits& defaultLimits(Phenomenon p);

// Defaults for `p` with the overrides from `sensor`'s params applied (`sensor` may be null).
PlausibilityLimits resolveLimits(Phenomenon p, const config::SensorConfig* sensor);

class QualityChecker {
 public:
  // Without a config only the defaults apply. `cfg` must outlive the checker.
  explicit QualityChecker(const config::StationConfig* cfg = nullptr) : cfg_(cfg) {}

  // Returns the quality flags to OR into `m.quality`. Updates the per-series history.
  uint16_t check(const Measurement& m);
  void reset();

 private:
  struct History {
    bool hasLast = false;
    float last = 0.0f;
    uint32_t lastTimestamp = 0;
    uint8_t sameCount = 0;
  };

  static constexpr uint8_t kPhenomena = static_cast<uint8_t>(Phenomenon::Count);
  const config::StationConfig* cfg_;
  History history_[config::kMaxSensors][kPhenomena];
};
