#pragma once

#include <stddef.h>

#include "MeasurementSink.h"

// Forwards every measurement to up to kMax sinks (e.g. window means to the log and the SD card).
class SinkFanout : public MeasurementSink {
 public:
  static constexpr size_t kMax = 4;

  bool add(MeasurementSink* sink) {
    if (sink == nullptr || count_ >= kMax) return false;
    sinks_[count_++] = sink;
    return true;
  }

  void onMeasurement(const Measurement& m) override {
    for (size_t i = 0; i < count_; ++i) sinks_[i]->onMeasurement(m);
  }

 private:
  MeasurementSink* sinks_[kMax] = {};
  size_t count_ = 0;
};
