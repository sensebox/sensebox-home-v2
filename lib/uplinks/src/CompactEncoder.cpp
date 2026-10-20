#include "CompactEncoder.h"

#include <math.h>

namespace compact {
namespace {

constexpr Codec kTemperature = {2, true, 100.0f};
constexpr Codec kHumidity = {1, false, 2.0f};
constexpr Codec kPm = {2, false, 10.0f};

// Valid raw range; the value just outside it is the "missing" marker.
int32_t rawMin(const Codec& c) { return c.isSigned ? -(1 << (8 * c.size - 1)) + 1 : 0; }
int32_t rawMax(const Codec& c) { return c.isSigned ? (1 << (8 * c.size - 1)) - 1 : (1 << (8 * c.size)) - 2; }
uint32_t missingMarker(const Codec& c) { return c.isSigned ? 1u << (8 * c.size - 1) : (1u << (8 * c.size)) - 1; }

void put(uint32_t bits, uint8_t size, uint8_t* out) {
  for (uint8_t i = 0; i < size; ++i) out[i] = static_cast<uint8_t>(bits >> (8 * (size - 1 - i)));
}

}  // namespace

const Codec* codec(Phenomenon p) {
  switch (p) {
    case Phenomenon::AirTemperature: return &kTemperature;
    case Phenomenon::RelativeHumidity: return &kHumidity;
    case Phenomenon::PM1:
    case Phenomenon::PM2_5:
    case Phenomenon::PM4:
    case Phenomenon::PM10: return &kPm;
    default: return nullptr;
  }
}

size_t frameSize(const config::LoRaWanConfig& cfg) {
  size_t n = kTimeSize;
  for (uint8_t c = 0; c < cfg.channelCount; ++c) {
    const Codec* k = codec(cfg.channels[c].phenomenon);
    if (k != nullptr) n += k->size;
  }
  return n;
}

EncodeResult encode(const config::LoRaWanConfig& cfg, uint32_t utc, const Measurement* values, size_t count,
                    uint8_t* out, size_t cap) {
  EncodeResult r;
  if (cap < frameSize(cfg)) return r;
  put(utc, kTimeSize, out);
  size_t pos = kTimeSize;
  for (uint8_t c = 0; c < cfg.channelCount; ++c) {
    const config::LppChannel& ch = cfg.channels[c];
    const Codec* k = codec(ch.phenomenon);
    if (k == nullptr) continue;
    const Measurement* match = nullptr;
    for (size_t i = 0; i < count; ++i) {
      if (values[i].sensorSlot == ch.slot && values[i].phenomenon == ch.phenomenon) match = &values[i];
    }
    if (match == nullptr || isnan(match->value)) {
      put(missingMarker(*k), k->size, out + pos);
      ++r.missing;
    } else {
      const float v = roundf(match->value * k->factor);
      int32_t raw;
      if (v < static_cast<float>(rawMin(*k))) {
        raw = rawMin(*k);
        ++r.clamped;
      } else if (v > static_cast<float>(rawMax(*k))) {
        raw = rawMax(*k);
        ++r.clamped;
      } else {
        raw = static_cast<int32_t>(v);
      }
      put(static_cast<uint32_t>(raw), k->size, out + pos);  // two's complement for signed
      ++r.encoded;
    }
    pos += k->size;
  }
  r.length = pos;
  return r;
}

}  // namespace compact
