#include "Airtime.h"

namespace airtime {

uint32_t loraMs(uint8_t sf, size_t phyPayloadLen) {
  if (sf < 7 || sf > 12) return 0;
  // Semtech AN1200.13 formula, computed in microseconds with integer maths.
  const uint32_t tSymUs = (1u << sf) * 8u;  // 2^SF / 125 kHz = 2^SF * 8 µs
  const int32_t de = sf >= 11 ? 1 : 0;      // low data rate optimisation
  const int32_t cr = 1;                     // 4/5
  const int32_t numerator = 8 * static_cast<int32_t>(phyPayloadLen) - 4 * sf + 28 + 16;  // CRC on, explicit header
  const int32_t denominator = 4 * (sf - 2 * de);
  int32_t blocks = numerator > 0 ? (numerator + denominator - 1) / denominator : 0;
  const uint32_t payloadSymbols = 8u + static_cast<uint32_t>(blocks * (cr + 4));
  // preamble: 8 + 4.25 symbols = 12.25 symbols
  const uint32_t preambleUs = tSymUs * 49u / 4u;
  return (preambleUs + payloadSymbols * tSymUs + 999u) / 1000u;
}

void Budget::refill(uint32_t nowMs) {
  if (!started_) {
    started_ = true;
    lastRefillMs_ = nowMs;
    return;
  }
  const uint32_t elapsed = nowMs - lastRefillMs_;
  lastRefillMs_ = nowMs;
  availableMs_ += static_cast<int64_t>(elapsed) * capacityMs_ / kDayMs;
  if (availableMs_ > capacityMs_) availableMs_ = capacityMs_;
}

bool Budget::canSpend(uint32_t costMs, uint32_t nowMs) {
  refill(nowMs);
  return availableMs_ >= static_cast<int64_t>(costMs);
}

void Budget::spend(uint32_t costMs, uint32_t nowMs) {
  refill(nowMs);
  availableMs_ -= costMs;
}

int32_t Budget::availableMs(uint32_t nowMs) {
  refill(nowMs);
  return static_cast<int32_t>(availableMs_);
}

}  // namespace airtime
