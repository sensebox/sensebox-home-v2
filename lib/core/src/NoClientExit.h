#pragma once

#include <stdint.h>

// Service mode (plan §4.8): restart into normal mode once no device has been connected for
// `idleExitMs`. The timer only starts after the first device joined (the user may need a while to
// get the phone out). Independent of WiFi (testable on the host).
class NoClientExit {
 public:
  void begin(uint32_t nowMs, uint32_t idleExitMs) {
    idleExitMs_ = idleExitMs;
    armed_ = false;
    lastSeenMs_ = nowMs;
  }

  // Returns true when service mode should end.
  bool update(uint8_t clients, uint32_t nowMs) {
    if (idleExitMs_ == 0) return false;
    if (clients > 0) {
      armed_ = true;
      lastSeenMs_ = nowMs;
      return false;
    }
    return armed_ && nowMs - lastSeenMs_ >= idleExitMs_;
  }

  // Seconds until the exit, 0 if it isn't counting (a device is connected, or not armed yet).
  uint32_t secondsLeft(uint8_t clients, uint32_t nowMs) const {
    if (idleExitMs_ == 0 || !armed_ || clients > 0) return 0;
    const uint32_t idle = nowMs - lastSeenMs_;
    return idle >= idleExitMs_ ? 0 : (idleExitMs_ - idle + 999) / 1000;
  }

 private:
  uint32_t idleExitMs_ = 0;
  uint32_t lastSeenMs_ = 0;
  bool armed_ = false;
};
