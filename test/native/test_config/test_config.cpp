#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "ConfigCodec.h"
#include "ConfigMigrator.h"
#include "StationConfig.h"

using namespace config;

namespace {

// Loads a file relative to the project root (pio test runs from there).
size_t loadFile(const char* path, char* buf, size_t cap) {
  FILE* f = fopen(path, "rb");
  if (f == nullptr) return 0;
  const size_t n = fread(buf, 1, cap - 1, f);
  fclose(f);
  buf[n] = '\0';
  return n;
}

char g_json[8192];
StationConfig g_cfg;
StationConfig g_cfg2;

}  // namespace

void setUp() { setDefaults(g_cfg); }
void tearDown() {}

void test_olpe_template_decodes() {
  const size_t n = loadFile("test/fixtures/olpe.json", g_json, sizeof(g_json));
  TEST_ASSERT_TRUE_MESSAGE(n > 0, "test/fixtures/olpe.json not found");
  const DecodeResult r = decode(g_json, n, g_cfg);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
  TEST_ASSERT_FALSE(r.migrated);

  // The name is set per station at provisioning; the template leaves it empty.
  TEST_ASSERT_EQUAL(2, g_cfg.sensorCount);
  TEST_ASSERT_EQUAL_STRING("am2315c", g_cfg.sensors[0].type);
  TEST_ASSERT_EQUAL_STRING("sps30", g_cfg.sensors[1].type);
  TEST_ASSERT_EQUAL_FLOAT(4.0f, g_cfg.sensors[1].param("fanCleaningIntervalDays", 0));
  TEST_ASSERT_EQUAL_FLOAT(7.0f, g_cfg.sensors[1].param("missing", 7.0f));

  TEST_ASSERT_EQUAL(1, g_cfg.uplinkCount);
  const LoRaWanConfig& lw = g_cfg.uplinks[0].lorawan;
  TEST_ASSERT_EQUAL(static_cast<int>(UplinkType::LoRaWanCayenne), static_cast<int>(g_cfg.uplinks[0].type));
  TEST_ASSERT_EQUAL_UINT32(600, lw.intervalSec);
  TEST_ASSERT_EQUAL_UINT32(72u * 3600u, lw.timeSyncIntervalSec);
  TEST_ASSERT_EQUAL(6, lw.channelCount);
  TEST_ASSERT_EQUAL(static_cast<int>(LppType::AnalogInput), static_cast<int>(lw.channels[2].type));
  TEST_ASSERT_EQUAL(static_cast<int>(Phenomenon::PM2_5), static_cast<int>(lw.channels[2].phenomenon));
}

void test_encode_decode_roundtrip() {
  const size_t n = loadFile("test/fixtures/olpe.json", g_json, sizeof(g_json));
  TEST_ASSERT_TRUE(decode(g_json, n, g_cfg).ok);
  g_cfg.sensors[0].calibrations[0] = {Phenomenon::AirTemperature, 1.01f, -0.4f};
  g_cfg.sensors[0].calibrationCount = 1;
  strcpy(g_cfg.deploymentJson, "{\"lat\":51.03,\"lon\":7.85}");

  char out[4096];
  const size_t len = encode(g_cfg, out, sizeof(out));
  TEST_ASSERT_TRUE(len > 0);
  setDefaults(g_cfg2);
  const DecodeResult r = decode(out, len, g_cfg2);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
  const Calibration* cal = g_cfg2.sensors[0].calibration(Phenomenon::AirTemperature);
  TEST_ASSERT_NOT_NULL(cal);
  TEST_ASSERT_EQUAL_FLOAT(-0.4f, cal->offset);
  TEST_ASSERT_EQUAL_STRING("{\"lat\":51.03,\"lon\":7.85}", g_cfg2.deploymentJson);
  TEST_ASSERT_EQUAL(g_cfg.uplinks[0].lorawan.channelCount, g_cfg2.uplinks[0].lorawan.channelCount);

  // Too-small buffer: 0 and an empty string, never a truncated document.
  char tiny[32];
  TEST_ASSERT_EQUAL(0, encode(g_cfg, tiny, sizeof(tiny)));
  TEST_ASSERT_EQUAL_STRING("", tiny);
}

void expectRejected(const char* json, const char* expectedFragment) {
  strcpy(g_cfg.name, "keep-me");
  const DecodeResult r = decode(json, strlen(json), g_cfg);
  TEST_ASSERT_FALSE_MESSAGE(r.ok, json);
  TEST_ASSERT_NOT_NULL_MESSAGE(strstr(r.error, expectedFragment), r.error);
  // a failed decode never modifies the output
  TEST_ASSERT_EQUAL_STRING("keep-me", g_cfg.name);
}

void test_invalid_configs_are_rejected() {
  expectRejected("{not json", "invalid JSON");
  expectRejected("{\"station\":{}}", "schemaVersion");
  expectRejected("{\"schemaVersion\":99}", "newer");
  expectRejected("{\"schemaVersion\":1,\"sensors\":[{\"slot\":0}]}", "type: required");
  expectRejected("{\"schemaVersion\":1,\"sensors\":[{\"type\":\"sps30\"}]}", "slot: required");
  expectRejected("{\"schemaVersion\":1,\"sensors\":[{\"slot\":0,\"type\":\"a\"},{\"slot\":0,\"type\":\"b\"}]}",
                 "duplicate slot");
  expectRejected("{\"schemaVersion\":1,\"sensors\":[{\"slot\":0,\"type\":\"a\",\"mode\":\"fast\"}]}",
                 "unknown field");
  expectRejected("{\"schemaVersion\":1,\"sensors\":[{\"slot\":0,\"type\":\"a\",\"intervalSec\":0}]}",
                 "out of range");
  expectRejected(
      "{\"schemaVersion\":1,\"uplinks\":[{\"type\":\"lorawan-cayenne\",\"channels\":"
      "[{\"ch\":1,\"slot\":3,\"phenomenon\":\"pm10\",\"lpp\":\"analog_in\"}]}]}",
      "not configured");
  expectRejected(
      "{\"schemaVersion\":1,\"uplinks\":[{\"type\":\"lorawan-cayenne\",\"devEui\":\"XYZ\"}]}",
      "16 hex digits");
  expectRejected("{\"schemaVersion\":1,\"uplinks\":[{\"type\":\"carrier-pigeon\"}]}", "type: unknown");
  expectRejected("{\"schemaVersion\":1,\"storage\":{\"lowWatermarkPct\":30,\"highWatermarkPct\":20}}",
                 "lowWatermarkPct");
  expectRejected("{\"schemaVersion\":1,\"station\":{\"name\":\"0123456789012345678901234567890123456789\"}}",
                 "longer than");
}

void test_eui_validation() {
  TEST_ASSERT_TRUE(isValidEui(""));
  TEST_ASSERT_TRUE(isValidEui("70B3D57ED0001234"));
  TEST_ASSERT_TRUE(isValidEui("70b3d57ed0001234"));
  TEST_ASSERT_FALSE(isValidEui("70B3D57ED000123"));
  TEST_ASSERT_FALSE(isValidEui("70B3D57ED000123G"));
}

// --- Migration framework, tested with a fake history: v1 {"interval":60} -> v2 {"measurement":{...}} --

bool fakeV1toV2(JsonObject root, char* err, size_t errLen) {
  if (!root["interval"].is<uint32_t>()) {
    snprintf(err, errLen, "v1: interval missing");
    return false;
  }
  root["measurement"]["intervalSeconds"] = root["interval"].as<uint32_t>();
  root.remove("interval");
  return true;
}

bool fakeV2toV3(JsonObject root, char*, size_t) {
  root["measurement"]["aggregate"] = true;
  return true;
}

constexpr MigrationStep kFakeSteps[] = {fakeV1toV2, fakeV2toV3};
constexpr MigrationTable kFakeTable = {kFakeSteps, 2, 1};

void test_migration_chain() {
  JsonDocument doc;
  deserializeJson(doc, "{\"schemaVersion\":1,\"interval\":60}");
  char err[64] = {};
  uint16_t from = 0;
  TEST_ASSERT_EQUAL(static_cast<int>(MigrationStatus::Migrated),
                    static_cast<int>(migrate(doc.as<JsonObject>(), 3, kFakeTable, err, sizeof(err), &from)));
  TEST_ASSERT_EQUAL_UINT16(1, from);
  TEST_ASSERT_EQUAL(3, doc["schemaVersion"].as<int>());
  TEST_ASSERT_EQUAL(60, doc["measurement"]["intervalSeconds"].as<int>());
  TEST_ASSERT_TRUE(doc["measurement"]["aggregate"].as<bool>());
  TEST_ASSERT_TRUE(doc["interval"].isNull());
}

void test_migration_errors() {
  char err[64] = {};
  JsonDocument doc;

  deserializeJson(doc, "{\"schemaVersion\":3}");
  TEST_ASSERT_EQUAL(static_cast<int>(MigrationStatus::Current),
                    static_cast<int>(migrate(doc.as<JsonObject>(), 3, kFakeTable, err, sizeof(err))));

  deserializeJson(doc, "{\"schemaVersion\":1}");
  TEST_ASSERT_EQUAL(static_cast<int>(MigrationStatus::StepFailed),
                    static_cast<int>(migrate(doc.as<JsonObject>(), 3, kFakeTable, err, sizeof(err))));
  TEST_ASSERT_EQUAL_STRING("v1: interval missing", err);

  deserializeJson(doc, "{\"schemaVersion\":0}");
  TEST_ASSERT_EQUAL(static_cast<int>(MigrationStatus::NoPath),
                    static_cast<int>(migrate(doc.as<JsonObject>(), 3, kFakeTable, err, sizeof(err))));

  deserializeJson(doc, "{\"schemaVersion\":1,\"interval\":60}");
  TEST_ASSERT_EQUAL(static_cast<int>(MigrationStatus::NoPath),
                    static_cast<int>(migrate(doc.as<JsonObject>(), 5, kFakeTable, err, sizeof(err))));
}

void test_decode_uses_migrations() {
  // decode() with a table that knows how to get from "v0" to the current schema
  static const MigrationStep steps[] = {[](JsonObject root, char*, size_t) {
    root["station"]["name"] = root["boxName"].as<const char*>();
    root.remove("boxName");
    return true;
  }};
  const MigrationTable table = {steps, 1, 0};
  const char* json = "{\"schemaVersion\":0,\"boxName\":\"legacy\"}";
  const DecodeResult r = decode(json, strlen(json), g_cfg, table);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
  TEST_ASSERT_TRUE(r.migrated);
  TEST_ASSERT_EQUAL_UINT16(0, r.fromVersion);
  TEST_ASSERT_EQUAL_STRING("legacy", g_cfg.name);
  TEST_ASSERT_EQUAL_UINT16(kSchemaVersion, g_cfg.schemaVersion);
}

// The example configs in README.md must stay valid.
void test_readme_examples_decode() {
  const char* quickstart =
      "{\"schemaVersion\":1,\"station\":{\"name\":\"my-test-box\"},\"sensors\":[{\"slot\":0,\"type\":"
      "\"hdc1080\",\"intervalSec\":60}]}";
  DecodeResult r = decode(quickstart, strlen(quickstart), g_cfg);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
  TEST_ASSERT_EQUAL(1, g_cfg.sensorCount);
  TEST_ASSERT_EQUAL(0, g_cfg.uplinkCount);

  const char* lorawan =
      "{\"schemaVersion\":1,\"station\":{\"name\":\"my-test-box\"},\"sensors\":[{\"slot\":0,\"type\":"
      "\"hdc1080\",\"intervalSec\":60}],\"uplinks\":[{\"type\":\"lorawan-cayenne\",\"intervalSec\":600,"
      "\"channels\":[{\"ch\":1,\"slot\":0,\"phenomenon\":\"air_temperature\",\"lpp\":\"temperature\"},"
      "{\"ch\":2,\"slot\":0,\"phenomenon\":\"relative_humidity\",\"lpp\":\"relative_humidity\"}]}]}";
  r = decode(lorawan, strlen(lorawan), g_cfg);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
  TEST_ASSERT_EQUAL(2, g_cfg.uplinks[0].lorawan.channelCount);

  size_t n = loadFile("test/fixtures/olpe.json", g_json, sizeof(g_json));
  r = decode(g_json, n, g_cfg);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);

  // The full example in the README's Configuration section (first ```json block).
  static char readme[65536];
  n = loadFile("README.md", readme, sizeof(readme));
  TEST_ASSERT_TRUE(n > 0);
  char* start = strstr(readme, "```json\n");
  TEST_ASSERT_NOT_NULL(start);
  start += strlen("```json\n");
  char* end = strstr(start, "```");
  TEST_ASSERT_NOT_NULL(end);
  r = decode(start, static_cast<size_t>(end - start), g_cfg);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
  TEST_ASSERT_EQUAL(2, g_cfg.sensorCount);
  TEST_ASSERT_NOT_NULL(g_cfg.sensors[0].calibration(Phenomenon::AirTemperature));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_olpe_template_decodes);
  RUN_TEST(test_encode_decode_roundtrip);
  RUN_TEST(test_invalid_configs_are_rejected);
  RUN_TEST(test_eui_validation);
  RUN_TEST(test_migration_chain);
  RUN_TEST(test_migration_errors);
  RUN_TEST(test_decode_uses_migrations);
  RUN_TEST(test_readme_examples_decode);
  return UNITY_END();
}
