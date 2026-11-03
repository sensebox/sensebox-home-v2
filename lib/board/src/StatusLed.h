#pragma once

#include <atomic>
#include <stdint.h>

#include "Health.h"
#include "LedPattern.h"

// On-board RGB LED:
//   normal operation   green, with blinks for active conditions (see LedPattern.h)
//   critical error     red (safe mode, or config rejected): normal operation is halted
//   service mode       blue: normal operation is halted
// Brightness is kept low: the LED sits in an outdoor housing and draws current.
//
// The LED runs in its own task: the main loop only passes every second, too slow for the
// 300 ms blinks.

class StatusLed {
 public:
  enum class Mode : uint8_t { Off, Booting, Health, ServiceMode, SafeMode };

  void begin();
  // Starts the LED task; from then on it shows `health` by itself.
  bool start(const HealthRegistry& health);
  void setMode(Mode mode) { mode_ = mode; }
  Mode mode() const { return mode_; }

 private:
  static void taskMain(void* self);
  void tick(uint32_t nowMs);
  void show(LedColor color);

  std::atomic<Mode> mode_{Mode::Booting};
  const HealthRegistry* health_ = nullptr;
  LedColor lastColor_ = LedColor::Off;
  bool shown_ = false;
};
