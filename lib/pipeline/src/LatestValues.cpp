#include "LatestValues.h"

void LatestValues::onMeasurement(const Measurement& m) {
  if (m.kind == ValueKind::Aggregated) return;
  for (size_t i = 0; i < count_; ++i) {
    Measurement& item = items_[i];
    if (item.sensorSlot != m.sensorSlot || item.phenomenon != m.phenomenon) continue;
    // A corrected value replaces the raw one of the same reading, never the other way round.
    if (m.kind == ValueKind::Raw && item.kind == ValueKind::Corrected && item.timestamp == m.timestamp) return;
    item = m;
    return;
  }
  if (count_ < kMax) items_[count_++] = m;
}
