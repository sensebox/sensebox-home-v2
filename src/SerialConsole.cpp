#include "SerialConsole.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "ConfigCodec.h"
#include "ConfigEdit.h"
#include "Log.h"
#include "PersistentStore.h"
#include "SystemInfo.h"
#include "Version.h"

namespace {
constexpr const char* kTag = "console";

// Set from the USB event task when the receive buffer overflowed (bytes were dropped).
volatile bool g_rxOverflow = false;

#if ARDUINO_USB_CDC_ON_BOOT
void onUsbCdcEvent(void*, esp_event_base_t, int32_t eventId, void*) {
  if (eventId == ARDUINO_USB_CDC_RX_OVERFLOW_EVENT) g_rxOverflow = true;
}
#endif

// Shared scratch buffers (only used from the main loop).
char g_out[4096];
config::StationConfig g_edit;

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

bool parseHex(const char* hex, uint8_t* out, size_t bytes) {
  if (strlen(hex) != bytes * 2) return false;
  for (size_t i = 0; i < bytes; ++i) {
    char pair[3] = {hex[2 * i], hex[2 * i + 1], '\0'};
    if (!isxdigit(static_cast<unsigned char>(pair[0])) || !isxdigit(static_cast<unsigned char>(pair[1]))) return false;
    out[i] = static_cast<uint8_t>(strtoul(pair, nullptr, 16));
  }
  return true;
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
  if (strcmp(line, "help") == 0) {
    Serial.println("commands:");
    Serial.println("  status                             health, sensors, LoRaWAN, memory");
    Serial.println("  config                             print the stored config (JSON, no secrets)");
    Serial.println("  config-set <json>                  store a complete config (one line)");
    Serial.println("  config-set name <name>             station name");
    Serial.println("  config-set uplinks devEui <hex>    LoRaWAN DevEUI (16 hex digits, as in TTN)");
    Serial.println("  config-set uplinks joinEui <hex>   LoRaWAN JoinEUI / AppEUI");
    Serial.println("  appkey <32 hex>                    store the LoRaWAN AppKey (MSB first); never printed");
    Serial.println("  log                                print the recent log lines");
    Serial.println("  reboot                             restart (also leaves safe mode)");
    Serial.println("config-set and appkey apply after 'reboot'");
  } else if (strcmp(line, "status") == 0) {
    printStatus(nowMs);
  } else if (strcmp(line, "config") == 0) {
    printConfig();
  } else if (strcmp(line, "config-set") == 0 && arg != nullptr) {
    configSet(arg);
  } else if (strcmp(line, "appkey") == 0 && arg != nullptr) {
    setAppKey(arg);
  } else if (strcmp(line, "log") == 0) {
    printLog();
  } else if (strcmp(line, "reboot") == 0) {
    // A requested reboot is a normal start: it never counts as a crash and ends safe mode.
    // (The reset reason "software" already does that; clearing the counter here makes it explicit.)
    persist::saveUnhealthyBoots(0);
    Serial.println(app_.safeMode ? "rebooting, leaving safe mode" : "rebooting");
    Serial.flush();
    delay(100);
    ESP.restart();
  } else {
    Serial.println("unknown command, try 'help'");
  }
}

void SerialConsole::printStatus(uint32_t nowMs) {
  Serial.printf("firmware %s (%s), uptime %lu s, reset: %s%s%s\n", SBH_FW_VERSION, app_.safeMode ? "SAFE MODE" : "normal",
                static_cast<unsigned long>(app_.clock.uptimeS()), sysinfo::resetReason(),
                sysinfo::previousBreadcrumb()[0] ? ", last at " : "", sysinfo::previousBreadcrumb());
  Serial.printf("time: %s, utc %lu\n", app_.clock.isSynced() ? "synced" : "not synced",
                static_cast<unsigned long>(app_.clock.nowUtc()));
  Serial.printf("heap: free %lu, min %lu, largest block %lu\n", static_cast<unsigned long>(sysinfo::freeHeap()),
                static_cast<unsigned long>(sysinfo::minFreeHeap()), static_cast<unsigned long>(sysinfo::largestFreeBlock()));

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
}

// Prints the stored config (what applies after the next reboot) and says whether it differs
// from the running one.
void SerialConsole::printConfig() {
  if (!loadEditableConfig(g_edit)) return;
  static char running[sizeof(g_out)];  // too big for the loop task stack
  const size_t runningLen = config::encode(app_.config, running, sizeof(running));
  if (config::encode(g_edit, g_out, sizeof(g_out)) == 0) {
    Serial.println("error: config too large for output buffer");
    return;
  }
  const bool pending = runningLen == 0 || strcmp(running, g_out) != 0;
  config::encode(g_edit, g_out, sizeof(g_out), true);
  Serial.println(g_out);
  if (pending) Serial.println("(stored config differs from the running one: reboot to apply)");
}

void SerialConsole::configSet(char* arg) {
  if (arg[0] == '{') {
    setConfig(arg);
  } else {
    setConfigField(arg);
  }
}

void SerialConsole::setConfig(const char* json) {
  const size_t len = strlen(json);
  const config::DecodeResult r = config::decode(json, len, g_edit);
  if (!r.ok) {
    // Report the received length: if it is shorter than what was sent, the line got cut.
    Serial.printf("error: %s (received %u characters)\n", r.error, static_cast<unsigned>(len));
    return;
  }
  if (!storeConfig(g_edit)) return;
  LOGI(kTag, "new config stored");
  Serial.println("ok: config stored, reboot to apply");
}

void SerialConsole::setConfigField(char* arg) {
  char* rest = arg;
  char* first = nextToken(rest);
  config::Field field = config::fieldFromPath(nullptr, first);
  if (field == config::Field::Unknown) {
    char* second = nextToken(rest);
    field = config::fieldFromPath(first, second);
  }
  if (field == config::Field::Unknown) {
    Serial.println("error: unknown field, see 'help'");
    return;
  }
  // The rest of the line is the value (names may contain spaces, EUIs may be grouped).
  if (!loadEditableConfig(g_edit)) return;
  char err[96];
  if (!config::setField(g_edit, field, rest, err, sizeof(err))) {
    Serial.printf("error: %s\n", err);
    return;
  }
  if (!storeConfig(g_edit)) return;
  LOGI(kTag, "config field updated");
  Serial.println("ok: stored, reboot to apply (see 'config' for the stored values)");
}

// Edits start from the stored config, so several config-set commands before a reboot add up.
// If nothing is stored yet, they start from the running config.
bool SerialConsole::loadEditableConfig(config::StationConfig& out) {
  const size_t len = persist::loadConfigJson(g_out, sizeof(g_out));
  if (len == 0) {
    out = app_.config;
    return true;
  }
  const config::DecodeResult r = config::decode(g_out, len, out);
  if (!r.ok) {
    Serial.printf("error: stored config is invalid (%s); send a complete config first\n", r.error);
    return false;
  }
  return true;
}

bool SerialConsole::storeConfig(const config::StationConfig& cfg) {
  // Round trip through the codec: stores the normalised current-schema form and re-validates
  // the whole document, so a single-field edit can't produce a config that fails at boot.
  static config::StationConfig check;
  const size_t len = config::encode(cfg, g_out, sizeof(g_out));
  if (len == 0) {
    Serial.println("error: config too large");
    return false;
  }
  const config::DecodeResult r = config::decode(g_out, len, check);
  if (!r.ok) {
    Serial.printf("error: %s\n", r.error);
    return false;
  }
  if (!persist::saveConfigJson(g_out)) {
    Serial.println("error: could not store config");
    return false;
  }
  return true;
}

void SerialConsole::setAppKey(const char* hex) {
  uint8_t key[16];
  if (!parseHex(hex, key, sizeof(key))) {
    Serial.println("error: AppKey must be 32 hex digits");
    return;
  }
  const bool ok = persist::setSecret(persist::secret::kLoRaAppKey, key, sizeof(key));
  memset(key, 0, sizeof(key));
  Serial.println(ok ? "ok: AppKey stored, reboot to apply" : "error: could not store AppKey");
  if (ok) LOGI(kTag, "LoRaWAN AppKey updated");
}

void SerialConsole::printLog() {
  static LogLine lines[logger::kRingLines];
  const size_t n = logger::copyRecent(lines, logger::kRingLines);
  for (size_t i = 0; i < n; ++i) {
    Serial.printf("[%7lu] %c %-8s %s\n", static_cast<unsigned long>(lines[i].uptimeS), logger::levelLetter(lines[i].level),
                  lines[i].tag, lines[i].msg);
  }
}
