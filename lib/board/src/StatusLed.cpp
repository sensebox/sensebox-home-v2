#include "StatusLed.h"

#include <Arduino.h>

namespace {
constexpr uint8_t kLevel = 24;  // of 255
constexpr uint32_t kBlinkOnMs = 80;

bool blinkPhase(uint32_t nowMs, uint32_t periodMs) { return nowMs % periodMs < kBlinkOnMs; }
}  // namespace

void StatusLed::begin() { show(0, 0, kLevel); }

void StatusLed::show(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t color = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
  if (color == lastColor_) return;  // avoid re-sending the same colour every tick
  lastColor_ = color;
  rgbLedWrite(PIN_RGB_LED, r, g, b);
}

void StatusLed::tick(uint32_t nowMs, ComponentStatus overall) {
  switch (mode_) {
    case Mode::Off: show(0, 0, 0); break;
    case Mode::Booting: show(0, 0, kLevel); break;
    case Mode::ServiceMode: show(kLevel, 0, kLevel); break;
    case Mode::SafeMode:
      if ((nowMs / 500) % 2 == 0) {
        show(kLevel, 0, 0);
      } else {
        show(kLevel, 0, kLevel);
      }
      break;
    case Mode::Health:
      switch (overall) {
        case ComponentStatus::Error:
          blinkPhase(nowMs, 1000) ? show(kLevel, 0, 0) : show(0, 0, 0);
          break;
        case ComponentStatus::Degraded:
          blinkPhase(nowMs, 2000) ? show(kLevel, kLevel / 2, 0) : show(0, 0, 0);
          break;
        default:
          blinkPhase(nowMs, 5000) ? show(0, kLevel, 0) : show(0, 0, 0);
          break;
      }
      break;
  }
}
