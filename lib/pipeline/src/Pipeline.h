#pragma once

#include <stddef.h>

#include "MeasurementSink.h"
#include "QualityChecker.h"
#include "StationConfig.h"

// Raw measurement -> quality flags -> (optional) calibrated copy -> all registered sinks
// (storage, uplinks, live view). See plan §4.4.

class Pipeline : public MeasurementSink {
 public:
  static constexpr size_t kMaxSinks = 6;

  // `cfg` must outlive the pipeline (it is the global station config).
  explicit Pipeline(const config::StationConfig& cfg) : cfg_(cfg), checker_(&cfg) {}

  bool addSink(MeasurementSink* sink);
  void onMeasurement(const Measurement& raw) override;

 private:
  void emit(const Measurement& m);

  const config::StationConfig& cfg_;
  QualityChecker checker_;
  MeasurementSink* sinks_[kMaxSinks] = {};
};
