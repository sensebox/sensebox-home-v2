#pragma once

#include <stddef.h>

#include "StationConfig.h"

// Single-field edits of the station config, used by the serial console (and later the REST API).
// Each edit is validated the same way as a full config document.

namespace config {

enum class Field : unsigned char {
  Unknown = 0,
  Name,           // station.name
  LoRaWanDevEui,  // first LoRaWAN uplink: devEui
  LoRaWanJoinEui, // first LoRaWAN uplink: joinEui
};

// Maps a console path to a field, case-insensitive: "name", "uplinks devEui", "uplinks joinEui"
// (`group` may be nullptr for top-level fields like "name").
Field fieldFromPath(const char* group, const char* key);

// Applies `value` to `cfg`. On failure `cfg` is unchanged and `err` describes the problem.
// EUIs accept separators (spaces, ':' and '-') and lower case; they are stored as 16 upper-case
// hex digits, MSB first, as the TTN console shows them.
bool setField(StationConfig& cfg, Field field, const char* value, char* err, size_t errLen);

}  // namespace config
