#pragma once

#include <stddef.h>
#include <stdint.h>

// senseBox MCU S2 board layer (plan §4.7.1). Pin names come from the board variant
// (variants/sensebox_home_v2/pins_arduino.h).

namespace board {

constexpr uint8_t kServiceButtonPin = 0;   // BOOT button, strapping pin: input only
constexpr uint32_t kSensorBusHz = 100000;  // SPS30 maximum
constexpr uint16_t kSensorBusTimeoutMs = 50;

// Starts the QWIIC sensor bus (Wire on SDA 39 / SCL 40) at 100 kHz with a bounded timeout.
void initSensorBus();

// True if SDA is held low while the bus should be idle (a slave hangs mid-transfer).
bool sensorBusStuck();

// Clocks out up to 9 SCL pulses plus a STOP to release a hung slave, then restarts Wire.
// Returns true if SDA is high afterwards.
bool recoverSensorBus();

// Probes all 7-bit addresses. Writes found addresses to `found`, returns the number found.
size_t scanSensorBus(uint8_t* found, size_t max);

// Raw button level (true = pressed).
bool serviceButtonPressed();

}  // namespace board
