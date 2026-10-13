#pragma once

#include <stdint.h>

// Crash-loop protection (plan §4.7). Pure decision logic; persistence of the counter is done
// by the caller (NVS on the device).
//
// Only abnormal resets (panic, watchdog, brownout) count as "boots without reaching healthy".
// A normal start (power-on, flashing, a requested reboot) resets the counter, so it also ends
// safe mode. Once the firmware has run healthy for kHealthyAfterMs, the counter is reset to 0.
// If the counter exceeds kMaxUnhealthyBoots, the box starts in safe mode (no sensors/uplinks,
// service mode only).

class BootGuard {
 public:
  static constexpr uint8_t kMaxUnhealthyBoots = 5;
  static constexpr uint32_t kHealthyAfterMs = 10u * 60u * 1000u;

  // Call once at boot with the persisted counter and whether the last reset was abnormal.
  // Returns the value to persist immediately.
  uint8_t onBoot(uint8_t persistedUnhealthyBoots, bool abnormalReset) {
    if (!abnormalReset) {
      unhealthyBoots_ = 0;
    } else {
      unhealthyBoots_ = persistedUnhealthyBoots < 255 ? persistedUnhealthyBoots + 1 : 255;
    }
    safeMode_ = unhealthyBoots_ > kMaxUnhealthyBoots;
    return unhealthyBoots_;
  }

  bool safeMode() const { return safeMode_; }
  uint8_t unhealthyBoots() const { return unhealthyBoots_; }

  // Call periodically. Returns true exactly once, when the counter should be persisted as 0.
  bool tick(uint32_t uptimeMs) {
    if (markedHealthy_ || uptimeMs < kHealthyAfterMs) return false;
    markedHealthy_ = true;
    unhealthyBoots_ = 0;
    return true;
  }

  bool markedHealthy() const { return markedHealthy_; }

 private:
  uint8_t unhealthyBoots_ = 0;
  bool safeMode_ = false;
  bool markedHealthy_ = false;
};
