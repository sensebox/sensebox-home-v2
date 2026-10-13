#include "Aggregator.h"

void Aggregator::setExpectedSamples(uint8_t slot, uint16_t expected) {
  if (slot < config::kMaxSensors) expected_[slot] = expected;
}

Aggregator::Series* Aggregator::find(uint8_t slot, Phenomenon p, ValueKind kind) {
  for (auto& s : series_) {
    if (s.used && s.slot == slot && s.phenomenon == p && s.kind == kind) return &s;
  }
  return nullptr;
}

Aggregator::Series* Aggregator::findOrCreate(uint8_t slot, Phenomenon p, ValueKind kind, Unit unit) {
  if (Series* s = find(slot, p, kind)) return s;
  for (auto& s : series_) {
    if (!s.used) {
      s = Series{};
      s.used = true;
      s.slot = slot;
      s.phenomenon = p;
      s.kind = kind;
      s.unit = unit;
      return &s;
    }
  }
  return nullptr;
}

void Aggregator::add(const Measurement& m) {
  if (m.kind == ValueKind::Aggregated) return;
  Series* s = findOrCreate(m.sensorSlot, m.phenomenon, m.kind, m.unit);
  if (s == nullptr) {
    ++dropped_;
    return;
  }
  if (m.quality & quality::kExcludeFromAggregate) {
    if (s->excluded < UINT16_MAX) ++s->excluded;
    return;
  }
  s->sum += m.value;
  if (s->count < UINT16_MAX) ++s->count;
  s->qualityOr |= m.quality;
}

size_t Aggregator::flush(uint32_t timestamp, uint16_t timestampQuality, MeasurementSink& sink) {
  size_t emitted = 0;
  for (auto& s : series_) {
    if (!s.used || s.count == 0) continue;
    // Prefer the corrected series of the same slot/phenomenon if it has data.
    if (s.kind == ValueKind::Raw) {
      const Series* corrected = find(s.slot, s.phenomenon, ValueKind::Corrected);
      if (corrected != nullptr && corrected->count > 0) continue;
    }
    Measurement m{};
    m.timestamp = timestamp;
    m.value = static_cast<float>(s.sum / s.count);
    m.sensorSlot = s.slot;
    m.phenomenon = s.phenomenon;
    m.unit = s.unit;
    m.kind = ValueKind::Aggregated;
    // The window timestamp decides about kTimeUnsynced, not the samples.
    m.quality = static_cast<uint16_t>((s.qualityOr & ~quality::kTimeUnsynced) | timestampQuality);
    const uint16_t expected = s.slot < config::kMaxSensors ? expected_[s.slot] : 0;
    if (expected > 0 && static_cast<uint32_t>(s.count) * 4 < static_cast<uint32_t>(expected) * 3) {
      m.quality |= quality::kIncompleteAgg;
    }
    sink.onMeasurement(m);
    ++emitted;
  }
  for (auto& s : series_) s = Series{};
  return emitted;
}
