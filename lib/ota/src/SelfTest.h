#pragma once

#include <stdint.h>

// Self-test of a freshly installed firmware (plan §4.9). Pure logic, host-tested.
//
// After an OTA update the new firmware starts in the bootloader state "pending verify". It is
// kept only if it runs normal operation for kPassAfterMs (2 min) without resetting and its
// configuration was accepted. Otherwise:
//   - a crash, watchdog reset or any other restart before that: the bootloader goes back to the
//     previous firmware by itself;
//   - a rejected configuration (e.g. a failed migration): Failed -> rollback.
// Sensors are deliberately not part of the test: a box with an unplugged sensor must still be
// updatable (the old firmware wouldn't measure either).

class SelfTest {
 public:
  static constexpr uint32_t kPassAfterMs = 2u * 60u * 1000u;

  enum class Result : uint8_t { NotNeeded, Running, Passed, Failed };

  void begin(bool pendingVerify) { state_ = pendingVerify ? Result::Running : Result::NotNeeded; }

  // Returns Passed or Failed exactly once (then act on it), Running while waiting.
  Result tick(uint32_t uptimeMs, bool configOk) {
    if (state_ != Result::Running) return Result::NotNeeded;  // not needed, or already decided
    if (uptimeMs < kPassAfterMs) return Result::Running;
    state_ = configOk ? Result::Passed : Result::Failed;
    return state_;
  }

  bool running() const { return state_ == Result::Running; }
  Result state() const { return state_; }
  uint32_t secondsLeft(uint32_t uptimeMs) const {
    return running() && uptimeMs < kPassAfterMs ? (kPassAfterMs - uptimeMs + 999) / 1000 : 0;
  }

 private:
  Result state_ = Result::NotNeeded;
};
