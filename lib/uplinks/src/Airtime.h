#pragma once

#include <stddef.h>
#include <stdint.h>

// LoRa airtime and the TTN fair-use budget (plan §6 "Airtime / TTN fair use").

namespace airtime {

// LoRaWAN overhead on top of the application payload: MHDR 1 + FHDR 7 + FPort 1 + MIC 4.
constexpr size_t kLoRaWanOverhead = 13;

// Airtime in ms of one LoRa frame, EU868 settings: BW 125 kHz, CR 4/5, explicit header, CRC on,
// 8 preamble symbols, low data rate optimisation for SF11/SF12. `phyPayloadLen` includes the
// LoRaWAN overhead. `sf` 7..12; returns 0 for other values.
uint32_t loraMs(uint8_t sf, size_t phyPayloadLen);

// Airtime of an uplink with `appPayloadLen` application bytes and `fOptsLen` MAC command bytes.
inline uint32_t uplinkMs(uint8_t sf, size_t appPayloadLen, size_t fOptsLen = 0) {
  return loraMs(sf, appPayloadLen + kLoRaWanOverhead + fOptsLen);
}

// Token bucket for TTN fair use: 30 s airtime per 24 h, refilled continuously.
class Budget {
 public:
  static constexpr uint32_t kDailyMs = 30000;
  static constexpr uint32_t kDayMs = 24u * 3600u * 1000u;

  explicit Budget(uint32_t dailyMs = kDailyMs) : capacityMs_(dailyMs), availableMs_(dailyMs) {}

  // True if `costMs` can be spent at `nowMs` (does not spend it).
  bool canSpend(uint32_t costMs, uint32_t nowMs);
  // Records airtime that was actually used (may go negative after a join burst).
  void spend(uint32_t costMs, uint32_t nowMs);
  int32_t availableMs(uint32_t nowMs);

 private:
  void refill(uint32_t nowMs);

  uint32_t capacityMs_;
  int64_t availableMs_;
  uint32_t lastRefillMs_ = 0;
  bool started_ = false;
};

}  // namespace airtime
