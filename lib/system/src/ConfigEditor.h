#pragma once

#include <stddef.h>

#include "StationConfig.h"

// Editing the stored configuration, shared by the serial console and the service web UI.
//
// Edits always start from the *stored* config (what applies after the next reboot), so several
// edits before one reboot add up. Every store re-validates the whole document, so a single-field
// edit can't produce a config the box rejects at boot. Changes apply after a reboot.
//
// Working memory (a JSON buffer and a config copy, ~6.5 KB) is allocated per call and freed
// again: there is no PSRAM, and static RAM is needed for WiFi. Call from the main loop only.

namespace cfgedit {

constexpr size_t kErrLen = 96;
constexpr size_t kJsonCap = 4096;  // largest config JSON (compact)

// Validates and stores a complete config given as JSON.
bool storeJson(const char* json, size_t len, char* err, size_t errLen);

// Sets one field, e.g. (nullptr, "name") or ("uplinks", "devEui"); see config::fieldFromPath.
bool setField(const char* group, const char* key, const char* value, const config::StationConfig& running,
              char* err, size_t errLen);

// Stores the LoRaWAN AppKey (32 hex digits, MSB first). Separators/spaces are not allowed.
bool setAppKeyHex(const char* hex, char* err, size_t errLen);

// Writes the stored config as JSON to `buf` (secrets are never part of it). Sets `differs` to true
// if it is not the config that is running. Returns the length, 0 on error.
size_t storedJson(const config::StationConfig& running, char* buf, size_t cap, bool pretty, bool& differs, char* err,
                  size_t errLen);

}  // namespace cfgedit
