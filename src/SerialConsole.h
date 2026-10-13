#pragma once

#include <stddef.h>
#include <stdint.h>

#include "App.h"

// Line-based console on the USB serial port, for development and production provisioning
// (tools/provision.py) until the web UI exists (phase 5). Type `help` for the commands.
// Secrets can be written but are never printed.

class SerialConsole {
 public:
  // Serial receive buffer: larger than one console line, so a pasted config fits completely.
  static constexpr size_t kRxBufferSize = 4096;

  explicit SerialConsole(App& app) : app_(app) {}
  // Registers for receive-overflow notifications. Call after Serial.begin().
  void begin();
  // Non-blocking: reads what is available and runs complete lines.
  void poll(uint32_t nowMs);

 private:
  void run(char* line, uint32_t nowMs);
  void printStatus(uint32_t nowMs);
  void printConfig();
  void configSet(char* arg);
  void setConfig(const char* json);
  void setConfigField(char* arg);
  bool loadEditableConfig(config::StationConfig& out);
  bool storeConfig(const config::StationConfig& cfg);
  void setAppKey(const char* hex);
  void printLog();

  App& app_;
  static constexpr size_t kLineLen = 3072;
  char line_[kLineLen] = {};
  size_t len_ = 0;
  bool overflow_ = false;
};
