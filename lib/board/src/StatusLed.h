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
// 300 ms blinks. If the task can't be created, setMode() and poll() draw from the caller
// instead (blinks are then coarse, but the colour of the mode is right).

class StatusLed {
 public:
  enum class Mode : uint8_t { Off, Booting, Health, ServiceMode, SafeMode };

  void begin();
  // Starts the LED task; from then on it shows `health` by itself.
  bool start(const HealthRegistry& health);
  void setMode(Mode mode);
  Mode mode() const { return mode_; }
  // Fallback for a missing task: call from the main loop. Does nothing while the task runs.
  void poll(uint32_t nowMs);

 private:
  static void taskMain(void* self);
  void tick(uint32_t nowMs);
  void show(LedColor color);

  std::atomic<Mode> mode_{Mode::Booting};
  const HealthRegistry* health_ = nullptr;
  bool taskRunning_ = false;  // set once in start(), before anything else draws
  LedColor lastColor_ = LedColor::Off;
  bool shown_ = false;
};
