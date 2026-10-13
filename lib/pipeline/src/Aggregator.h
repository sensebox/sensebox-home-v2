#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Measurement.h"
#include "MeasurementSink.h"
#include "StationConfig.h"

// Builds window means per (slot, phenomenon) (plan §4.4).
//
// Accepts Raw and Corrected values. If a series has Corrected values in a window, the mean is
// built from those, otherwise from the Raw values. Samples with quality::kExcludeFromAggregate
// flags are left out. Each uplink owns its own Aggregator, so different targets can use
// different windows.

class Aggregator {
 public:
  static constexpr size_t kMaxSeries = 24;

  // Expected number of samples per window for a slot (e.g. 600 s / 60 s = 10). Means built from
  // less than 75 % of this are flagged quality::kIncompleteAgg. 0 = no check.
  void setExpectedSamples(uint8_t slot, uint16_t expected);

  void add(const Measurement& m);

  // Emits one Aggregated measurement per series with at least one valid sample, then starts a
  // new window. `timestamp`/`timestampQuality` describe the window end. Returns the number emitted.
  size_t flush(uint32_t timestamp, uint16_t timestampQuality, MeasurementSink& sink);

  // Samples that didn't fit because all series slots were in use (should stay 0).
  uint32_t droppedSamples() const { return dropped_; }

 private:
  struct Series {
    bool used = false;
    uint8_t slot = 0;
    Phenomenon phenomenon = Phenomenon::Unknown;
    ValueKind kind = ValueKind::Raw;
    Unit unit = Unit::None;
    double sum = 0.0;
    uint16_t count = 0;
    uint16_t excluded = 0;
    uint16_t qualityOr = 0;  // flags of the included samples
  };

  Series* find(uint8_t slot, Phenomenon p, ValueKind kind);
  Series* findOrCreate(uint8_t slot, Phenomenon p, ValueKind kind, Unit unit);

  Series series_[kMaxSeries];
  uint16_t expected_[config::kMaxSensors] = {};
  uint32_t dropped_ = 0;
};
