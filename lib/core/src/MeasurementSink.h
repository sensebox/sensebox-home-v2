#pragma once

#include "Measurement.h"

// Anything that consumes measurements: the pipeline, storage, live view, uplink buffers.
class MeasurementSink {
 public:
  virtual ~MeasurementSink() = default;
  virtual void onMeasurement(const Measurement& m) = 0;
};
