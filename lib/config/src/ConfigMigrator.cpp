#include "ConfigMigrator.h"

#include <stdio.h>

namespace config {
namespace {

// Add steps here when the schema changes after the first release: kSteps[0] migrates v1 -> v2,
// and so on. Bump kSchemaVersion in StationConfig.h and add a fixture test for every new step.
// Until the first release, the schema is changed in place (no stations in the field yet).
constexpr MigrationStep* kSteps = nullptr;
constexpr MigrationTable kTable = {kSteps, 0, 1};

}  // namespace

const MigrationTable& migrations() { return kTable; }

MigrationStatus migrate(JsonObject root, uint16_t targetVersion, const MigrationTable& table, char* err,
                        size_t errLen, uint16_t* fromVersion) {
  if (!root["schemaVersion"].is<uint16_t>()) {
    snprintf(err, errLen, "schemaVersion missing or invalid");
    return MigrationStatus::MissingVersion;
  }
  uint16_t version = root["schemaVersion"].as<uint16_t>();
  if (fromVersion != nullptr) *fromVersion = version;

  if (version > targetVersion) {
    snprintf(err, errLen, "config schema %u is newer than supported %u", version, targetVersion);
    return MigrationStatus::TooNew;
  }
  if (version == targetVersion) return MigrationStatus::Current;

  while (version < targetVersion) {
    if (version < table.firstFrom || static_cast<size_t>(version - table.firstFrom) >= table.count) {
      snprintf(err, errLen, "no migration from schema %u", version);
      return MigrationStatus::NoPath;
    }
    const MigrationStep step = table.steps[version - table.firstFrom];
    if (!step(root, err, errLen)) return MigrationStatus::StepFailed;
    ++version;
    root["schemaVersion"] = version;
  }
  return MigrationStatus::Migrated;
}

}  // namespace config
