#include "SerialConsole.h"

#include <Arduino.h>
#include <string.h>

#include "BootMode.h"
#include "ConfigEdit.h"
#include "ConfigEditor.h"
#include "Log.h"
#include "PersistentStore.h"
#include "ServicePassword.h"
#include "SystemInfo.h"
#include "Updater.h"
#include "Version.h"

namespace {

// Set from the USB event task when the receive buffer overflowed (bytes were dropped).
volatile bool g_rxOverflow = false;

#if ARDUINO_USB_CDC_ON_BOOT
void onUsbCdcEvent(void*, esp_event_base_t, int32_t eventId, void*) {
  if (eventId == ARDUINO_USB_CDC_RX_OVERFLOW_EVENT) g_rxOverflow = true;
}
#endif

// Splits off the next space-separated token. Returns nullptr if there is none.
char* nextToken(char*& rest) {
  while (*rest == ' ') ++rest;
  if (*rest == '\0') return nullptr;
  char* token = rest;
  while (*rest != '\0' && *rest != ' ') ++rest;
  if (*rest == ' ') *rest++ = '\0';
  while (*rest == ' ') ++rest;
  return token;
}

void printResult(bool ok, const char* err, const char* okText) {
  if (ok) {
    Serial.println(okText);
  } else {
    Serial.printf("error: %s\n", err);
  }
}

}  // namespace

void SerialConsole::begin() {
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.onEvent(ARDUINO_USB_CDC_RX_OVERFLOW_EVENT, onUsbCdcEvent);
#endif
}

void SerialConsole::poll(uint32_t nowMs) {
  // Bounded per call so a flood of input can't starve the main loop.
  for (int i = 0; i < 256 && Serial.available() > 0; ++i) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n') {
      line_[len_] = '\0';
      if (g_rxOverflow) {
        // Running a command with missing bytes could store a half-edited value.
        Serial.println("error: input was cut off (serial receive buffer overflow), please send the line again");
      } else if (overflow_) {
        Serial.printf("error: line too long (max %u characters)\n", static_cast<unsigned>(kLineLen - 1));
      } else if (len_ > 0) {
        run(line_, nowMs);
      }
      len_ = 0;
      overflow_ = false;
      g_rxOverflow = false;
      continue;
    }
    if (len_ + 1 < kLineLen) {
      line_[len_++] = static_cast<char>(c);
    } else {
      overflow_ = true;
    }
  }
}

void SerialConsole::run(char* line, uint32_t nowMs) {
  char* arg = strchr(line, ' ');
  if (arg != nullptr) {
    *arg++ = '\0';
    while (*arg == ' ') ++arg;
  }
  char err[cfgedit::kErrLen] = {};

  if (strcmp(line, "help") == 0) {
    Serial.println("commands:");
    Serial.println("  status                             health, sensors, LoRaWAN, memory");
    Serial.println("  config                             print the stored config (JSON, no secrets)");
    Serial.println("  config-set <json>                  store a complete config (one line)");
    Serial.println("  config-set name <name>             station name");
    Serial.println("  config-set uplinks devEui <hex>    LoRaWAN DevEUI (16 hex digits, as in TTN)");
    Serial.println("  config-set uplinks joinEui <hex>   LoRaWAN JoinEUI / AppEUI");
    Serial.println("  appkey <32 hex>                    store the LoRaWAN AppKey (MSB first); never printed");
    Serial.println("  service-password <password>        WiFi password of service mode (8-63 characters)");
    Serial.println("  log                                print the end of the log (from the SD card)");
    Serial.println("  reboot                             restart into normal mode (also leaves safe/service mode)");
    Serial.println("config-set and appkey apply after 'reboot'. Service mode: hold BOOT for 3 s, then release.");
  } else if (strcmp(line, "status") == 0) {
    printStatus(nowMs);
  } else if (strcmp(line, "config") == 0) {
    printConfig();
  } else if (strcmp(line, "config-set") == 0 && arg != nullptr) {
    configSet(arg);
  } else if (strcmp(line, "appkey") == 0 && arg != nullptr) {
    printResult(cfgedit::setAppKeyHex(arg, err, sizeof(err)), err, "ok: AppKey stored, reboot to apply");
  } else if (strcmp(line, "service-password") == 0 && arg != nullptr) {
    const bool ok = servicepw::set(arg, err, sizeof(err));
    printResult(ok, err, "ok: service-mode password stored");
    if (ok) LOGI("console", "service-mode password changed");
  } else if (strcmp(line, "log") == 0) {
    printLog(nowMs);
  } else if (strcmp(line, "reboot") == 0) {
    // A requested reboot is a normal start: it never counts as a crash, ends safe mode, and
    // (the service-mode request being one-shot) also ends service mode.
    persist::saveUnhealthyBoots(0);
    app_.storage.flushNow(nowMs);  // don't lose the last seconds of log/measurements
    if (app_.selfTest.running()) Serial.println("note: the new firmware hasn't passed its self-test yet, so it is rolled back");
    Serial.println(app_.serviceMode ? "rebooting into normal mode" : (app_.safeMode ? "rebooting, leaving safe mode" : "rebooting"));
    Serial.flush();
    delay(100);
    ESP.restart();
  } else {
    Serial.println("unknown command, try 'help'");
  }
}

void SerialConsole::printStatus(uint32_t nowMs) {
  const char* mode = app_.serviceMode ? "SERVICE MODE" : (app_.safeMode ? "SAFE MODE" : "normal");
  Serial.printf("firmware %s (%s), uptime %lu s, reset: %s%s%s\n", SBH_FW_VERSION, mode,
                static_cast<unsigned long>(app_.clock.uptimeS()), sysinfo::resetReason(),
                sysinfo::previousBreadcrumb()[0] ? ", last at " : "", sysinfo::previousBreadcrumb());
  Serial.printf("heap: free %lu, min %lu, largest block %lu\n", static_cast<unsigned long>(sysinfo::freeHeap()),
                static_cast<unsigned long>(sysinfo::minFreeHeap()), static_cast<unsigned long>(sysinfo::largestFreeBlock()));
  const updater::RunningInfo fw = updater::bootInfo();
  Serial.printf("firmware slot: %s", fw.partition);
  if (app_.selfTest.running()) {
    Serial.printf(", NEW FIRMWARE: self-test running (%lu s left; restarting before that rolls back)",
                  static_cast<unsigned long>(app_.selfTest.secondsLeft(nowMs)));
  }
  if (fw.previousRolledBack) Serial.print(", an earlier update was rolled back");
  Serial.println();

  if (app_.serviceMode) {
    // Sensors and LoRaWAN don't run; show the status saved before the restart.
    Serial.println("nothing is measured in service mode.");
    Serial.printf("SAVED STATUS, NOT LIVE (saved about %lu s ago, before service mode started):\n",
                  static_cast<unsigned long>(app_.clock.uptimeS()));
    const StatusSnapshot& s = bootmode::snapshot();
    constexpr size_t kCap = 6144;
    auto buf = sysinfo::makeTempBuffer(kCap);
    if (!buf) {
      Serial.println("error: out of memory");
    } else if (!snapshot::isValid(s) || snapshot::toJson(s, buf.get(), kCap, false) == 0) {
      Serial.println("  (none)");
    } else {
      Serial.println(buf.get());
    }
    return;
  }

  Serial.printf("time: %s, utc %lu\n", app_.clock.isSynced() ? "synced" : "not synced",
                static_cast<unsigned long>(app_.clock.nowUtc()));
  Serial.printf("health: %s\n", componentStatusName(app_.health.overall()));
  for (size_t i = 0; i < app_.health.count(); ++i) {
    ComponentHealth h;
    if (!app_.health.get(static_cast<HealthRegistry::Id>(i), h)) continue;
    Serial.printf("  %-20s %-9s errors %lu%s%s\n", h.name, componentStatusName(h.status),
                  static_cast<unsigned long>(h.errorCount), h.lastError[0] ? ", last: " : "", h.lastError);
  }

  for (size_t i = 0; i < app_.sensors.slotCount(); ++i) {
    SensorSlotStatus s;
    if (!app_.sensors.status(i, nowMs, s)) continue;
    Serial.printf("sensor %u %-8s %-12s readings %lu, errors %lu (%s)", s.slot, s.type, sensorStateName(s.state),
                  static_cast<unsigned long>(s.readingCount), static_cast<unsigned long>(s.errorCount),
                  sensorErrorName(s.lastError));
    if (s.retryInMs > 0) Serial.printf(", retry in %lu s", static_cast<unsigned long>(s.retryInMs / 1000));
    Serial.println();
  }

  const LmicTransport::Status ls = app_.lora.status(nowMs);
  Serial.printf("lorawan: %s, AppKey %s, join attempts %lu, uplinks %lu, downlinks %lu, SF%u, RSSI %d, SNR %d, budget %ld ms\n",
                ls.joined ? "joined" : "not joined", persist::hasSecret(persist::secret::kLoRaAppKey) ? "set" : "missing",
                static_cast<unsigned long>(ls.joinAttempts), static_cast<unsigned long>(ls.uplinks),
                static_cast<unsigned long>(ls.downlinks), ls.spreadingFactor, ls.lastRssi, ls.lastSnr,
                static_cast<long>(ls.budgetMs));

  const SdStore::Status sd = app_.storage.status();
  if (!sd.enabled) {
    Serial.println("sd: not used (out of memory at boot)");
  } else if (!sd.mounted) {
    Serial.println("sd: NOT MOUNTED - the card is part of the hardware; see the 'sd' health line");
  } else {
    Serial.printf("sd: %s, %lu of %lu MB free%s, writing %s and %s\n", sd.full ? "FULL" : "ok",
                  static_cast<unsigned long>(sd.freeBytes >> 20), static_cast<unsigned long>(sd.totalBytes >> 20),
                  sd.smallCard ? " (smaller than 4 GB!)" : "", sd.dataFile, sd.logFile);
  }
  if (sd.enabled) {
    Serial.printf("    files deleted %lu, write errors %lu, dropped measurements %lu, dropped log lines %lu\n",
                  static_cast<unsigned long>(sd.filesDeleted), static_cast<unsigned long>(sd.writeErrors),
                  static_cast<unsigned long>(sd.droppedMeasurements), static_cast<unsigned long>(sd.droppedLogLines));
  }
}

// Prints the stored config (what applies after the next reboot) and says whether it differs
// from the running one.
void SerialConsole::printConfig() {
  char err[cfgedit::kErrLen];
  bool differs = false;
  constexpr size_t kCap = 6144;  // pretty-printed JSON is larger than the compact form
  auto buf = sysinfo::makeTempBuffer(kCap);
  if (!buf) {
    Serial.println("error: out of memory");
    return;
  }
  if (cfgedit::storedJson(app_.config, buf.get(), kCap, true, differs, err, sizeof(err)) == 0) {
    Serial.printf("error: %s\n", err);
    return;
  }
  Serial.println(buf.get());
  if (differs) Serial.println("(stored config differs from the running one: reboot to apply)");
}

void SerialConsole::configSet(char* arg) {
  char err[cfgedit::kErrLen];
  if (arg[0] == '{') {
    const size_t len = strlen(arg);
    if (!cfgedit::storeJson(arg, len, err, sizeof(err))) {
      // Report the received length: if it is shorter than what was sent, the line got cut.
      Serial.printf("error: %s (received %u characters)\n", err, static_cast<unsigned>(len));
      return;
    }
    Serial.println("ok: config stored, reboot to apply");
    return;
  }

  // Field edit: "name <value>" or "<group> <key> <value>". The rest of the line is the value
  // (names may contain spaces, EUIs may be grouped).
  char* rest = arg;
  char* first = nextToken(rest);
  const char* group = nullptr;
  const char* key = first;
  if (config::fieldFromPath(nullptr, first) == config::Field::Unknown) {
    group = first;
    key = nextToken(rest);
  }
  if (key == nullptr || config::fieldFromPath(group, key) == config::Field::Unknown) {
    Serial.println("error: unknown field, see 'help'");
    return;
  }
  printResult(cfgedit::setField(group, key, rest, app_.config, err, sizeof(err)), err,
              "ok: stored, reboot to apply (see 'config' for the stored values)");
}

void SerialConsole::printLog(uint32_t nowMs) {
  // Normal operation: the end of the log file on the SD card (the complete log is there).
  if (!app_.serviceMode) {
    constexpr size_t kTailBytes = 4096;  // ~40 lines; per call, freed afterwards
    auto buf = sysinfo::makeTempBuffer(kTailBytes);
    const SdStore::Status sd = app_.storage.status();
    if (buf && app_.storage.readLogTail(nowMs, buf.get(), kTailBytes) > 0) {
      Serial.printf("--- end of %s (complete log on the SD card) ---\n", sd.logFile);
      Serial.print(buf.get());
      return;
    }
    Serial.println(buf ? "--- SD card not available: newest lines from RAM only ---"
                       : "--- out of memory: newest lines from RAM only ---");
  }
  // Service mode (no SD card) or no card: the newest lines kept in RAM, one line at a time.
  LogLine line;
  for (size_t age = logger::lineCount(); age > 0; --age) {
    if (!logger::lineAt(age - 1, line)) continue;
    Serial.printf("[%7lu] %c %-8s %s\n", static_cast<unsigned long>(line.uptimeS), logger::levelLetter(line.level),
                  line.tag, line.msg);
  }
}
