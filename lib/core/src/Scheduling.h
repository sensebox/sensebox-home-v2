#pragma once

#include <stdint.h>

// Helpers for 32-bit millisecond timestamps that wrap after ~49 days.
// Always compare through these functions, never with `now >= due` directly.

// True if `now` has reached or passed `due` (valid while they are < 24 days apart).
inline bool timeReached(uint32_t now, uint32_t due) { return static_cast<int32_t>(now - due) >= 0; }

// Milliseconds from `now` until `due`, 0 if already reached.
inline uint32_t timeUntil(uint32_t now, uint32_t due) { return timeReached(now, due) ? 0 : due - now; }

// Exponential backoff: base * 2^attempt, capped at max. attempt 0 = base.
inline uint32_t backoffMs(uint32_t baseMs, uint32_t maxMs, uint8_t attempt) {
  uint64_t value = baseMs;
  for (uint8_t i = 0; i < attempt && value < maxMs; ++i) value *= 2;
  return value > maxMs ? maxMs : static_cast<uint32_t>(value);
}
