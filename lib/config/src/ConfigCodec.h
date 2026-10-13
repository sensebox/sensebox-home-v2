#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ConfigMigrator.h"
#include "StationConfig.h"

// JSON <-> StationConfig. Used at boot (load from NVS), by the REST API and for import/export.
// Uses ArduinoJson (heap), so it must not be called on the measurement path.

namespace config {

struct DecodeResult {
  bool ok = false;
  bool migrated = false;       // true if the document was upgraded; caller should store it again
  uint16_t fromVersion = 0;    // schema version of the input document
  char error[96] = {};
};

// Parses, migrates and validates `json`. On success writes `out`; on failure `out` is untouched.
DecodeResult decode(const char* json, size_t len, StationConfig& out,
                    const MigrationTable& table = migrations());

// Serialises `cfg` (pretty = human-readable export). Returns the length written, or 0 if
// `cap` is too small. The output is always NUL-terminated when cap > 0.
size_t encode(const StationConfig& cfg, char* buf, size_t cap, bool pretty = false);

// Validates hex EUI strings: empty (= not set) or exactly 16 hex digits.
bool isValidEui(const char* hex);

}  // namespace config
