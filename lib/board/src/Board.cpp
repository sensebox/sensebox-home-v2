#include "Board.h"

#include <Arduino.h>
#include <Wire.h>

#include "Log.h"

namespace board {
namespace {
constexpr const char* kTag = "board";
}

void initSensorBus() {
  Wire.begin(PIN_QWIIC_SDA, PIN_QWIIC_SCL, kSensorBusHz);
  Wire.setTimeOut(kSensorBusTimeoutMs);
}

bool sensorBusStuck() {
  // Only meaningful while no transfer is running; all sensor access happens in the main loop.
  return digitalRead(PIN_QWIIC_SDA) == LOW;
}

bool recoverSensorBus() {
  Wire.end();
  pinMode(PIN_QWIIC_SDA, INPUT_PULLUP);
  pinMode(PIN_QWIIC_SCL, OUTPUT_OPEN_DRAIN);
  for (int i = 0; i < 9 && digitalRead(PIN_QWIIC_SDA) == LOW; ++i) {
    digitalWrite(PIN_QWIIC_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_QWIIC_SCL, HIGH);
    delayMicroseconds(5);
  }
  // STOP condition: SDA low -> high while SCL is high.
  pinMode(PIN_QWIIC_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_QWIIC_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_QWIIC_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_QWIIC_SDA, HIGH);
  delayMicroseconds(5);
  pinMode(PIN_QWIIC_SDA, INPUT_PULLUP);
  const bool released = digitalRead(PIN_QWIIC_SDA) == HIGH;

  initSensorBus();
  if (released) {
    LOGI(kTag, "sensor bus recovered");
  } else {
    LOGE(kTag, "sensor bus still stuck after recovery");
  }
  return released;
}

size_t scanSensorBus(uint8_t* found, size_t max) {
  size_t n = 0;
  for (uint8_t addr = 0x08; addr < 0x78 && n < max; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) found[n++] = addr;
  }
  return n;
}

bool serviceButtonPressed() { return digitalRead(kServiceButtonPin) == LOW; }

}  // namespace board
