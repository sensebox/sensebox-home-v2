#pragma once

#include <stdint.h>

#include "Health.h"

// On-board RGB LED (plan §4.8). Shows one pattern at a time:
//   booting        blue, steady
//   ok             green blink every 5 s
//   degraded       yellow blink every 2 s
//   error          red blink every 1 s
//   service mode   purple, steady
//   safe mode      red/purple alternating
// Brightness is kept low: the LED sits in an outdoor housing and draws current.

class StatusLed {
 public:
  enum class Mode : uint8_t { Off, Booting, Health, ServiceMode, SafeMode };

  void begin();
  void setMode(Mode mode) { mode_ = mode; }
  Mode mode() const { return mode_; }
  void tick(uint32_t nowMs, ComponentStatus overall);

 private:
  void show(uint8_t r, uint8_t g, uint8_t b);

  Mode mode_ = Mode::Booting;
  uint32_t lastColor_ = 0xFFFFFFFF;
};
