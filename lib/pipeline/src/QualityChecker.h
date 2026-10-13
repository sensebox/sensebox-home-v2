#pragma once

#include <stdint.h>

#include "Measurement.h"
#include "StationConfig.h"

// Plausibility, spike and stuck-value checks (plan §4.4). Values are flagged, never dropped.
// The sensor's physical range (quality::kOutOfRange) is checked by the SensorManager, which
// knows the sensor descriptor.

struct PlausibilityLimits {
  float min;              // plausible environmental range
  float max;
  float maxDeltaPerMin;   // spike threshold; 0 = no spike check
  uint8_t stuckCount;     // identical consecutive values that count as stuck; 0 = no check
};

// Defaults for outdoor air measurements.
const PlausibilityLimits& defaultLimits(Phenomenon p);

class QualityChecker {
 public:
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
  History history_[config::kMaxSensors][kPhenomena];
};
