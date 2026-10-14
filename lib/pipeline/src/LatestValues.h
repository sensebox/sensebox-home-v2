#pragma once

#include <stddef.h>
#include <stdint.h>

#include "MeasurementSink.h"

// Keeps the most recent value per (sensor slot, phenomenon): raw, or corrected when calibration
// exists. Used for the status snapshot shown in service mode. Fixed size, no heap.

class LatestValues : public MeasurementSink {
 public:
  static constexpr size_t kMax = 16;

  void onMeasurement(const Measurement& m) override;

  size_t count() const { return count_; }
  const Measurement& at(size_t i) const { return items_[i]; }

 private:
  Measurement items_[kMax] = {};
  size_t count_ = 0;
};
