#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "LatestValues.h"
#include "StatusSnapshot.h"

namespace {

StatusSnapshot g_snap;
char g_json[8192];

void fill(StatusSnapshot& s) {
  snapshot::clear(s);
  snapshot::copyText(s.firmware, sizeof(s.firmware), "0.1.0-dev");
  snapshot::copyText(s.stationName, sizeof(s.stationName), "Olpe-017");
  snapshot::copyText(s.resetReason, sizeof(s.resetReason), "power_on");
  snapshot::copyText(s.overall, sizeof(s.overall), "degraded");
  s.uptimeS = 3600;
  s.utc = 1760000000u;
  s.componentCount = 1;
  snapshot::copyText(s.components[0].name, sizeof(s.components[0].name), "sensor1:sps30");
  snapshot::copyText(s.components[0].status, sizeof(s.components[0].status), "error");
  snapshot::copyText(s.components[0].lastError, sizeof(s.components[0].lastError), "not_found, retry in 40 s");
  s.components[0].errorCount = 3;
  s.sensorCount = 1;
  s.sensors[0].slot = 1;
  snapshot::copyText(s.sensors[0].type, sizeof(s.sensors[0].type), "sps30");
  snapshot::copyText(s.sensors[0].state, sizeof(s.sensors[0].state), "error");
  s.valueCount = 1;
  s.values[0].slot = 0;
  snapshot::copyText(s.values[0].phenomenon, sizeof(s.values[0].phenomenon), "air_temperature");
  snapshot::copyText(s.values[0].unit, sizeof(s.values[0].unit), "°C");
  s.values[0].value = 21.5f;
  s.lorawan.configured = true;
  s.lorawan.joined = true;
  s.lorawan.uplinks = 6;
  s.logCount = 1;
  s.log[0].uptimeS = 3599;
  s.log[0].level = 'W';
  snapshot::copyText(s.log[0].tag, sizeof(s.log[0].tag), "sensors");
  snapshot::copyText(s.log[0].msg, sizeof(s.log[0].msg), "slot 1: sps30 not_found");
}

Measurement make(uint8_t slot, Phenomenon p, float v, uint32_t ts, ValueKind k) {
  Measurement m{};
  m.timestamp = ts;
  m.value = v;
  m.sensorSlot = slot;
  m.phenomenon = p;
  m.unit = defaultUnit(p);
  m.kind = k;
  return m;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_crc32_reference_value() {
  // standard CRC-32 check value
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, snapshot::crc32("123456789", 9));
}

void test_seal_and_validate() {
  fill(g_snap);
  TEST_ASSERT_FALSE(snapshot::isValid(g_snap));  // not sealed yet
  snapshot::seal(g_snap);
  TEST_ASSERT_TRUE(snapshot::isValid(g_snap));

  g_snap.uptimeS += 1;  // any change after sealing is detected
  TEST_ASSERT_FALSE(snapshot::isValid(g_snap));
  snapshot::seal(g_snap);
  TEST_ASSERT_TRUE(snapshot::isValid(g_snap));

  snapshot::invalidate(g_snap);
  TEST_ASSERT_FALSE(snapshot::isValid(g_snap));
}

void test_random_memory_is_not_valid() {
  // RTC memory after power-on holds garbage
  uint8_t* raw = reinterpret_cast<uint8_t*>(&g_snap);
  for (size_t i = 0; i < sizeof(g_snap); ++i) raw[i] = static_cast<uint8_t>(i * 31 + 7);
  TEST_ASSERT_FALSE(snapshot::isValid(g_snap));
  // even with the right magic, the checksum doesn't match
  g_snap.magic = StatusSnapshot::kMagic;
  g_snap.version = StatusSnapshot::kVersion;
  g_snap.size = sizeof(StatusSnapshot);
  TEST_ASSERT_FALSE(snapshot::isValid(g_snap));
}

void test_fits_in_rtc_memory() {
  // ESP32-S2 RTC slow memory is 8 KB, shared with other RTC data; keep the snapshot well below.
  TEST_ASSERT_TRUE_MESSAGE(sizeof(StatusSnapshot) <= 4096, "StatusSnapshot grew beyond 4 KB");
}

void test_copy_text_truncates() {
  char small[5];
  snapshot::copyText(small, sizeof(small), "abcdefgh");
  TEST_ASSERT_EQUAL_STRING("abcd", small);
  snapshot::copyText(small, sizeof(small), nullptr);
  TEST_ASSERT_EQUAL_STRING("", small);
}

void test_json() {
  fill(g_snap);
  const size_t len = snapshot::toJson(g_snap, g_json, sizeof(g_json), true);
  TEST_ASSERT_TRUE(len > 0);
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, g_json) == DeserializationError::Ok);
  TEST_ASSERT_EQUAL_STRING("Olpe-017", doc["station"]);
  TEST_ASSERT_EQUAL_UINT32(1760000000u, doc["utc"].as<uint32_t>());
  TEST_ASSERT_EQUAL_STRING("error", doc["components"][0]["status"]);
  TEST_ASSERT_EQUAL_STRING("sps30", doc["sensors"][0]["type"]);
  TEST_ASSERT_EQUAL_FLOAT(21.5f, doc["values"][0]["value"].as<float>());
  TEST_ASSERT_TRUE(doc["lorawan"]["joined"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("W", doc["log"][0]["level"]);

  // without log
  snapshot::toJson(g_snap, g_json, sizeof(g_json), false);
  deserializeJson(doc, g_json);
  TEST_ASSERT_TRUE(doc["log"].isNull());

  // unsynced clock -> utc null
  g_snap.utc = 0;
  snapshot::toJson(g_snap, g_json, sizeof(g_json), false);
  deserializeJson(doc, g_json);
  TEST_ASSERT_TRUE(doc["utc"].isNull());

  // buffer too small: empty string, never a truncated document
  char tiny[64];
  TEST_ASSERT_EQUAL(0, snapshot::toJson(g_snap, tiny, sizeof(tiny), true));
  TEST_ASSERT_EQUAL_STRING("", tiny);
}

void test_full_snapshot_json_fits_service_buffer() {
  // Worst case: every list full, every string at maximum length.
  snapshot::clear(g_snap);
  memset(g_snap.firmware, 'x', sizeof(g_snap.firmware) - 1);
  g_snap.componentCount = StatusSnapshot::kMaxComponents;
  for (auto& c : g_snap.components) {
    memset(c.name, 'n', sizeof(c.name) - 1);
    memset(c.lastError, 'e', sizeof(c.lastError) - 1);
  }
  g_snap.sensorCount = StatusSnapshot::kMaxSensors;
  g_snap.valueCount = StatusSnapshot::kMaxValues;
  g_snap.logCount = StatusSnapshot::kMaxLogLines;
  for (auto& l : g_snap.log) {
    l.level = 'I';
    memset(l.msg, 'm', sizeof(l.msg) - 1);
  }

  TEST_ASSERT_TRUE(snapshot::toJson(g_snap, g_json, 8192, true) > 0);
  // GET /api/status sends it without the log in a 6 KB buffer, after a ~300 byte prefix.
  TEST_ASSERT_TRUE(snapshot::toJson(g_snap, g_json, 6144 - 400, false) > 0);
}

void test_latest_values() {
  LatestValues latest;
  latest.onMeasurement(make(0, Phenomenon::AirTemperature, 20.0f, 60, ValueKind::Raw));
  latest.onMeasurement(make(0, Phenomenon::AirTemperature, 19.5f, 60, ValueKind::Corrected));
  latest.onMeasurement(make(0, Phenomenon::RelativeHumidity, 50.0f, 60, ValueKind::Raw));
  latest.onMeasurement(make(0, Phenomenon::AirTemperature, 99.0f, 600, ValueKind::Aggregated));
  TEST_ASSERT_EQUAL(2, latest.count());
  TEST_ASSERT_EQUAL_FLOAT(19.5f, latest.at(0).value);  // corrected wins, aggregates are ignored

  latest.onMeasurement(make(0, Phenomenon::AirTemperature, 21.0f, 120, ValueKind::Raw));
  TEST_ASSERT_EQUAL_FLOAT(21.0f, latest.at(0).value);  // newer reading replaces

  for (uint8_t slot = 1; slot < 30; ++slot) latest.onMeasurement(make(slot, Phenomenon::PM10, 1.0f, 60, ValueKind::Raw));
  TEST_ASSERT_EQUAL(LatestValues::kMax, latest.count());  // bounded
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc32_reference_value);
  RUN_TEST(test_seal_and_validate);
  RUN_TEST(test_random_memory_is_not_valid);
  RUN_TEST(test_fits_in_rtc_memory);
  RUN_TEST(test_copy_text_truncates);
  RUN_TEST(test_json);
  RUN_TEST(test_full_snapshot_json_fits_service_buffer);
  RUN_TEST(test_latest_values);
  return UNITY_END();
}
