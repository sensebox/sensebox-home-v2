#pragma once

#include <stdint.h>

#include "Health.h"
#include "LedPattern.h"

// On-board RGB LED:
//   normal operation   green, with blinks for active conditions (see LedPattern.h)
//   critical error     red (safe mode, or config rejected): normal operation is halted
//   service mode       blue: normal operation is halted
// Brightness is kept low: the LED sits in an outdoor housing and draws current.

class StatusLed {
 public:
  enum class Mode : uint8_t { Off, Booting, Health, ServiceMode, SafeMode };

  void begin();
  void setMode(Mode mode) { mode_ = mode; }
  Mode mode() const { return mode_; }
  void tick(uint32_t nowMs, const HealthRegistry& health);

 private:
  void show(LedColor color);

  Mode mode_ = Mode::Booting;
  LedColor lastColor_ = LedColor::Off;
  bool shown_ = false;
};
