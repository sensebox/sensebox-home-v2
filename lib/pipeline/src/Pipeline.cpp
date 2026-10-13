#include "Pipeline.h"

bool Pipeline::addSink(MeasurementSink* sink) {
  for (auto& slot : sinks_) {
    if (slot == nullptr) {
      slot = sink;
      return true;
    }
  }
  return false;
}

void Pipeline::emit(const Measurement& m) {
  for (auto* sink : sinks_) {
    if (sink != nullptr) sink->onMeasurement(m);
  }
}

void Pipeline::onMeasurement(const Measurement& raw) {
  Measurement m = raw;
  m.quality |= checker_.check(raw);
  emit(m);

  const config::SensorConfig* sensor = cfg_.sensorBySlot(m.sensorSlot);
  const config::Calibration* cal = sensor != nullptr ? sensor->calibration(m.phenomenon) : nullptr;
  if (cal == nullptr) return;

  Measurement corrected = m;
  corrected.kind = ValueKind::Corrected;
  corrected.value = m.value * cal->gain + cal->offset;
  emit(corrected);
}
