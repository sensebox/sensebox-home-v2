#pragma once

#include <stdint.h>

#include "Health.h"

// Status LED pattern while the box runs normally: steady green, with a short blink in a
// signal colour every few seconds for each condition that is active. Several conditions
// blink one after another (e.g. green ... yellow, red ... green).
// Pure logic, so it can be tested on the host; StatusLed turns the colour into the LED.

enum class LedColor : uint8_t { Off, Green, White, Yellow, Red, Purple, Blue };

struct LedSignals {
  bool settingUp = false;      // white: joining LoRaWAN or sensors not started yet
  bool connectFailed = false;  // yellow: LoRaWAN join failed / link lost / DevEUI or AppKey missing
  bool sensorFailed = false;   // red blink: one or more sensors failed
  bool storageFailed = false;  // purple: SD card can't be read or written
  bool critical = false;       // steady red: config rejected, normal operation can't run
};

namespace ledpattern {

constexpr uint32_t kPeriodMs = 3000;  // one blink sequence every 3 s
constexpr uint32_t kBlinkMs = 300;    // length of one blink
constexpr uint32_t kGapMs = 300;      // green between two blinks of one sequence

// Derives the LED conditions from the health registry (by component name).
LedSignals fromHealth(const HealthRegistry& health);

// Colour to show at `nowMs` for normal operation.
LedColor colorAt(const LedSignals& signals, uint32_t nowMs);

}  // namespace ledpattern
