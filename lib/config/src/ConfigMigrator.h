#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

// Schema migrations for the stored configuration (plan §5).
//
// A migration step converts a document from version N to N+1 in place. Steps are pure functions
// on the JSON document, so each one can be unit-tested with a fixture of the old format.
// A config is never thrown away: if migration fails, the caller keeps the original document.

namespace config {

using MigrationStep = bool (*)(JsonObject root, char* err, size_t errLen);

struct MigrationTable {
  const MigrationStep* steps;  // steps[i] migrates version (firstFrom + i) -> (firstFrom + i + 1)
  size_t count;
  uint16_t firstFrom;
};

// The firmware's real migration history.
const MigrationTable& migrations();

enum class MigrationStatus : uint8_t {
  Current = 0,     // document already at target version, nothing changed
  Migrated,        // document was upgraded
  MissingVersion,  // no schemaVersion field
  TooNew,          // document is newer than this firmware understands
  NoPath,          // no step available for a version in between
  StepFailed,      // a step reported an error
};

// Migrates `root` to `targetVersion`. On failure `root` may be partially modified, so callers
// must work on a copy if they need the original.
MigrationStatus migrate(JsonObject root, uint16_t targetVersion, const MigrationTable& table, char* err,
                        size_t errLen, uint16_t* fromVersion = nullptr);

}  // namespace config
