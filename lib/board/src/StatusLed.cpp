#include "StatusLed.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr uint8_t kLevel = 24;  // of 255
constexpr uint32_t kTickMs = 100;  // fine enough for 300 ms blinks
constexpr uint32_t kTaskStack = 2560;
constexpr UBaseType_t kTaskPriority = 1;  // same as the Arduino loop task
}  // namespace

// Green from the start: the white "setting up" blinks begin once the task runs.
void StatusLed::begin() { show(LedColor::Green); }

bool StatusLed::start(const HealthRegistry& health) {
  health_ = &health;
  taskRunning_ = xTaskCreate(taskMain, "led", kTaskStack, this, kTaskPriority, nullptr) == pdPASS;
  return taskRunning_;
}

void StatusLed::setMode(Mode mode) {
  mode_ = mode;
  if (!taskRunning_) tick(millis());  // no task: show the new mode right away
}

void StatusLed::poll(uint32_t nowMs) {
  if (!taskRunning_) tick(nowMs);
}

void StatusLed::taskMain(void* self) {
  auto* led = static_cast<StatusLed*>(self);
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    led->tick(millis());
    vTaskDelayUntil(&last, pdMS_TO_TICKS(kTickMs));
  }
}

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

void StatusLed::tick(uint32_t nowMs) {
  switch (mode_.load()) {
    case Mode::Off: show(LedColor::Off); break;
    case Mode::Booting: show(LedColor::Green); break;
    case Mode::ServiceMode: show(LedColor::Blue); break;
    case Mode::SafeMode: show(LedColor::Red); break;
    case Mode::Health:
      if (health_ == nullptr) {
        show(LedColor::Green);
      } else {
        show(ledpattern::colorAt(ledpattern::fromHealth(*health_), nowMs));
      }
      break;
  }
}
