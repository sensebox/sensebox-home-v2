#include "CayenneEncoder.h"

#include <math.h>

namespace cayenne {
namespace {

using config::LppType;

// Scales, rounds and clamps to [lo, hi]. NaN becomes 0 and counts as clamped.
int32_t scaled(float value, float factor, int32_t lo, int32_t hi, bool& clamped) {
  if (isnan(value)) {
    clamped = true;
    return 0;
  }
  const float v = roundf(value * factor);
  if (v < static_cast<float>(lo)) {
    clamped = true;
    return lo;
  }
  if (v > static_cast<float>(hi)) {
    clamped = true;
    return hi;
  }
  return static_cast<int32_t>(v);
}

}  // namespace

uint8_t typeCode(LppType t) {
  switch (t) {
    case LppType::DigitalInput: return 0x00;
    case LppType::AnalogInput: return 0x02;
    case LppType::Illuminance: return 0x65;
    case LppType::Temperature: return 0x67;
    case LppType::RelativeHumidity: return 0x68;
    case LppType::BarometricPressure: return 0x73;
    default: return 0xFF;
  }
}

uint8_t dataSize(LppType t) {
  switch (t) {
    case LppType::DigitalInput:
    case LppType::RelativeHumidity: return 1;
    case LppType::AnalogInput:
    case LppType::Illuminance:
    case LppType::Temperature:
    case LppType::BarometricPressure: return 2;
    default: return 0;
  }
}

size_t encodeValue(uint8_t channel, LppType type, float value, uint8_t* out, size_t cap, bool& clamped) {
  const uint8_t size = dataSize(type);
  if (size == 0 || cap < 2u + size) return 0;

  int32_t raw = 0;
  switch (type) {
    case LppType::DigitalInput: raw = scaled(value, 1.0f, 0, 255, clamped); break;
    case LppType::RelativeHumidity: raw = scaled(value, 2.0f, 0, 255, clamped); break;
    case LppType::AnalogInput: raw = scaled(value, 100.0f, -32768, 32767, clamped); break;
    case LppType::Illuminance: raw = scaled(value, 1.0f, 0, 65535, clamped); break;
    case LppType::Temperature: raw = scaled(value, 10.0f, -32768, 32767, clamped); break;
    case LppType::BarometricPressure: raw = scaled(value, 10.0f, 0, 65535, clamped); break;
    default: return 0;
  }

  out[0] = channel;
  out[1] = typeCode(type);
  const uint16_t bits = static_cast<uint16_t>(raw);  // two's complement for signed types
  if (size == 1) {
    out[2] = static_cast<uint8_t>(bits);
  } else {
    out[2] = static_cast<uint8_t>(bits >> 8);
    out[3] = static_cast<uint8_t>(bits & 0xFF);
  }
  return 2u + size;
}

EncodeResult encode(const config::LoRaWanConfig& cfg, const Measurement* values, size_t count, uint8_t* out,
                    size_t cap) {
  EncodeResult r;
  for (uint8_t c = 0; c < cfg.channelCount; ++c) {
    const config::LppChannel& ch = cfg.channels[c];
    const Measurement* match = nullptr;
    for (size_t i = 0; i < count; ++i) {
      if (values[i].sensorSlot == ch.slot && values[i].phenomenon == ch.phenomenon) match = &values[i];
    }
    if (match == nullptr) {
      ++r.missing;
      continue;
    }
    bool clamped = false;
    const size_t n = encodeValue(ch.channel, ch.type, match->value, out + r.length, cap - r.length, clamped);
    if (n == 0) {
      ++r.overflow;
      continue;
    }
    r.length += n;
    ++r.encoded;
    if (clamped) ++r.clamped;
  }
  return r;
}

}  // namespace cayenne
