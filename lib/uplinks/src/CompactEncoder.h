#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Measurement.h"
#include "StationConfig.h"

// "lorawan-compact" frame, layout version 1 (sent on port 2). Big endian throughout:
//
//   bytes 0-3   window end, Unix seconds (uint32); 0 = clock not synced yet
//   then        one value per configured channel, in config order, no channel or type bytes:
//                 air_temperature    int16   0.01 °C    missing = 0x8000
//                 relative_humidity  uint8   0.5 %      missing = 0xFF
//                 pm1/pm2_5/pm4/pm10 uint16  0.1 µg/m³  missing = 0xFFFF
//
// Out-of-range values are clamped to the largest valid value (never the "missing" marker).
// The TTN side is tools/ttn-uplink-formatter.js; keep both in sync. A new layout gets a new port.

namespace compact {

constexpr uint8_t kPort = 2;
constexpr size_t kTimeSize = 4;

struct Codec {
  uint8_t size;  // 1 or 2 bytes
  bool isSigned;
  float factor;  // raw = round(value * factor)
};

// Encoding of a phenomenon; nullptr if it can't be sent.
const Codec* codec(Phenomenon p);

// Frame length for a config (time + all channels).
size_t frameSize(const config::LoRaWanConfig& cfg);

struct EncodeResult {
  size_t length = 0;    // bytes written; 0 if `cap` is too small
  uint8_t encoded = 0;  // channels with a value
  uint8_t missing = 0;  // channels sent as "missing"
  uint8_t clamped = 0;  // values clamped to the codec's range
};

// For each configured channel, the matching measurement (same slot and phenomenon) from
// `values`; if several match, the last one wins.
EncodeResult encode(const config::LoRaWanConfig& cfg, uint32_t utc, const Measurement* values, size_t count,
                    uint8_t* out, size_t cap);

}  // namespace compact
