#include "StatusLed.h"

#include <Arduino.h>

namespace {
constexpr uint8_t kLevel = 24;  // of 255
}  // namespace

// Green from the start: setup() doesn't tick the LED, and the white "setting up" blinks
// begin with the first loop.
void StatusLed::begin() { show(LedColor::Green); }

void StatusLed::show(LedColor color) {
  if (shown_ && color == lastColor_) return;  // avoid re-sending the same colour every tick
  shown_ = true;
  lastColor_ = color;
  uint8_t r = 0, g = 0, b = 0;
  switch (color) {
    case LedColor::Off: break;
    case LedColor::Green: g = kLevel; break;
    case LedColor::White: r = g = b = kLevel; break;
    case LedColor::Yellow: r = kLevel; g = kLevel; break;
    case LedColor::Red: r = kLevel; break;
    case LedColor::Purple: r = kLevel; b = kLevel; break;
    case LedColor::Blue: b = kLevel; break;
  }
  rgbLedWrite(PIN_RGB_LED, r, g, b);
}

void StatusLed::tick(uint32_t nowMs, const HealthRegistry& health) {
  switch (mode_) {
    case Mode::Off: show(LedColor::Off); break;
    case Mode::Booting: show(LedColor::Green); break;
    case Mode::ServiceMode: show(LedColor::Blue); break;
    case Mode::SafeMode: show(LedColor::Red); break;
    case Mode::Health: show(ledpattern::colorAt(ledpattern::fromHealth(health), nowMs)); break;
  }
}
