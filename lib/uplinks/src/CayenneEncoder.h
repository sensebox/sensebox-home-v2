#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Measurement.h"
#include "StationConfig.h"

// Cayenne LPP encoding (plan §6). Each value becomes [channel][type][data, big endian].
// The frame must stay decodable by TTN's CayenneLPP payload formatter, which the openSenseMap
// TTN integration relies on.

namespace cayenne {

constexpr uint8_t kPort = 1;

// LoRaWAN EU868 maximum application payload at the slowest data rate (DR0 / SF12).
constexpr size_t kMaxPayload = 51;

uint8_t typeCode(config::LppType t);
// Encoded data size (without channel and type bytes); 0 for unknown types.
uint8_t dataSize(config::LppType t);

struct EncodeResult {
  size_t length = 0;    // bytes written to the output buffer
  uint8_t encoded = 0;  // channels written
  uint8_t missing = 0;  // configured channels without a value (left out of the frame)
  uint8_t clamped = 0;  // values clamped to the type's range
  uint8_t overflow = 0; // channels skipped because the frame was full
};

// Encodes, for each configured channel, the matching measurement (same slot and phenomenon)
// from `values`. If several match, the last one wins. Channels without a value are omitted.
EncodeResult encode(const config::LoRaWanConfig& cfg, const Measurement* values, size_t count, uint8_t* out,
                    size_t cap);

// Encodes a single value. Returns bytes written (0 if `cap` is too small or the type is unknown).
// Sets `clamped` if the value had to be limited to the type's range.
size_t encodeValue(uint8_t channel, config::LppType type, float value, uint8_t* out, size_t cap, bool& clamped);

}  // namespace cayenne
