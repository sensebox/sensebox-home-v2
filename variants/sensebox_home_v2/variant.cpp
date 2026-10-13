// Board initialisation for the senseBox:home v2 firmware on the senseBox MCU S2.
//
// Same power-on defaults as the stock `sensebox_mcu_esp32s2` variant, but WITHOUT its boot-time
// button handling: the stock variant blocks for 5 s at every boot and, if the BOOT button (GPIO0)
// is pressed and released in that window, switches the boot partition to ota_1 and restarts.
// With our partition table ota_1 is the second firmware slot, so that would bypass the OTA
// rollback logic. GPIO0 is handled by the firmware instead (service mode, plan §4.8).

#include <esp_chip_info.h>

#include "esp32-hal-gpio.h"
#include "pins_arduino.h"

extern "C" {

void initVariant(void) {
  // Sensor ports (QWIIC / IO) powered by default.
  pinMode(IO_ENABLE, OUTPUT);
  digitalWrite(IO_ENABLE, LOW);

  // RGB LED off.
  pinMode(PIN_RGB_LED, OUTPUT);
  digitalWrite(PIN_RGB_LED, LOW);

  // XBee socket 1 (LoRa-Bee) powered by default.
  pinMode(PIN_XB1_ENABLE, OUTPUT);
  digitalWrite(PIN_XB1_ENABLE, LOW);

  // UART port only on chips without PSRAM (as in the stock variant).
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  if (chip_info.revision <= 0) {
    pinMode(PIN_UART_ENABLE, OUTPUT);
    digitalWrite(PIN_UART_ENABLE, LOW);
  }

  // On-board photodiode sensor enabled (as in the stock variant).
  pinMode(PD_ENABLE, OUTPUT);
  digitalWrite(PD_ENABLE, HIGH);

  // BOOT button: input only, never driven (strapping pin).
  pinMode(0, INPUT_PULLUP);
}
}
