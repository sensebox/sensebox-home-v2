#pragma once

#include <stdint.h>

#include "Scheduling.h"

// Debounced short/long press detection, independent of the GPIO (testable on the host).
// Long press fires once while the button is still held, so the user gets feedback (LED) without
// having to guess when to release.

class ButtonDetector {
 public:
  enum class Event : uint8_t { None, ShortPress, LongPress };

  explicit ButtonDetector(uint32_t longPressMs = 3000, uint32_t debounceMs = 30)
      : longPressMs_(longPressMs), debounceMs_(debounceMs) {}

  Event update(bool pressed, uint32_t nowMs) {
    if (pressed != raw_) {
      raw_ = pressed;
      rawChangedMs_ = nowMs;
    }
    if (raw_ != stable_ && nowMs - rawChangedMs_ >= debounceMs_) {
      stable_ = raw_;
      if (stable_) {
        pressedSinceMs_ = nowMs;
        longFired_ = false;
      } else if (!longFired_) {
        return Event::ShortPress;
      }
    }
    if (stable_ && !longFired_ && nowMs - pressedSinceMs_ >= longPressMs_) {
      longFired_ = true;
      return Event::LongPress;
    }
    return Event::None;
  }

  bool pressed() const { return stable_; }

 private:
  uint32_t longPressMs_;
  uint32_t debounceMs_;
  bool raw_ = false;
  bool stable_ = false;
  bool longFired_ = false;
  uint32_t rawChangedMs_ = 0;
  uint32_t pressedSinceMs_ = 0;
};
