#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "CayenneUplink.h"
#include "ConfigCodec.h"

namespace {

uint64_t g_uptimeMs = 0;
uint64_t fakeUptimeMs() { return g_uptimeMs; }
uint32_t fakeUptimeS() { return static_cast<uint32_t>(g_uptimeMs / 1000); }

class FakeTransport : public LoRaWanTransport {
 public:
  bool joined() const override { return isJoined; }
  bool busy() const override { return isBusy; }
  bool send(uint8_t port, const uint8_t* data, size_t len, bool requestTime) override {
    ++sends;
    lastPort = port;
    lastLen = len;
    memcpy(last, data, len);
    lastRequestTime = requestTime;
    return accept;
  }
  bool isJoined = true;
  bool isBusy = false;
  bool accept = true;
  int sends = 0;
  uint8_t lastPort = 0;
  size_t lastLen = 0;
  uint8_t last[64] = {};
  bool lastRequestTime = false;
};

class CollectSink : public MeasurementSink {
 public:
  void onMeasurement(const Measurement& m) override { ++count; last = m; }
  int count = 0;
  Measurement last{};
};

config::StationConfig g_cfg;

void loadOlpe() {
  static char json[8192];
  FILE* f = fopen("config/olpe-template.json", "rb");
  TEST_ASSERT_NOT_NULL(f);
  const size_t n = fread(json, 1, sizeof(json) - 1, f);
  fclose(f);
  const config::DecodeResult r = config::decode(json, n, g_cfg);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
}

Measurement raw(uint8_t slot, Phenomenon p, float v) {
  Measurement m{};
  m.timestamp = static_cast<uint32_t>(g_uptimeMs / 1000);
  m.value = v;
  m.sensorSlot = slot;
  m.phenomenon = p;
  m.unit = defaultUnit(p);
  m.kind = ValueKind::Raw;
  m.quality = quality::kTimeUnsynced;
  return m;
}

// Feeds one full 60 s measurement of both Olpe sensors.
void feedMinute(CayenneUplink& up, float temp) {
  up.onMeasurement(raw(0, Phenomenon::AirTemperature, temp));
  up.onMeasurement(raw(0, Phenomenon::RelativeHumidity, 60.0f));
  up.onMeasurement(raw(1, Phenomenon::PM1, 1.0f));
  up.onMeasurement(raw(1, Phenomenon::PM2_5, 2.0f));
  up.onMeasurement(raw(1, Phenomenon::PM4, 3.0f));
  up.onMeasurement(raw(1, Phenomenon::PM10, 4.0f));
}

void runMinutes(CayenneUplink& up, int minutes, float temp = 20.0f) {
  for (int i = 0; i < minutes; ++i) {
    g_uptimeMs += 60000;
    feedMinute(up, temp);
    up.tick(static_cast<uint32_t>(g_uptimeMs));
  }
}

}  // namespace

void setUp() {
  g_uptimeMs = 0;
  loadOlpe();
}
void tearDown() {}

void test_sends_window_mean_every_10_minutes() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  FakeTransport t;
  CollectSink storage;
  CayenneUplink up(g_cfg, g_cfg.uplinks[0].lorawan, clock, health, t);
  up.setAggregateSink(&storage);
  up.begin(0);

  runMinutes(up, 9);
  TEST_ASSERT_EQUAL(0, t.sends);
  runMinutes(up, 1, 20.0f);
  TEST_ASSERT_EQUAL(1, t.sends);
  TEST_ASSERT_EQUAL(CayenneUplink::kPort, t.lastPort);
  TEST_ASSERT_EQUAL(23, t.lastLen);
  TEST_ASSERT_TRUE(t.lastRequestTime);  // clock not synced yet
  TEST_ASSERT_EQUAL(6, storage.count);  // window means also go to storage
  TEST_ASSERT_EQUAL(static_cast<int>(ValueKind::Aggregated), static_cast<int>(storage.last.kind));

  // temperature mean of this window: 5 min at 10 °C, 5 min at 30 °C = 20 °C -> 0x00C8
  runMinutes(up, 5, 10.0f);
  runMinutes(up, 5, 30.0f);
  TEST_ASSERT_EQUAL(2, t.sends);
  const uint8_t temp[] = {0x01, 0x67, 0x00, 0xC8};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(temp, t.last, 4);
}

void test_time_sync_requested_only_when_due() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  FakeTransport t;
  CayenneUplink up(g_cfg, g_cfg.uplinks[0].lorawan, clock, health, t);
  up.begin(0);
  runMinutes(up, 10);
  TEST_ASSERT_TRUE(t.lastRequestTime);

  clock.setUtc(1760000000000ull, g_uptimeMs, TimeSource::LoRaWan);
  runMinutes(up, 10);
  TEST_ASSERT_FALSE(t.lastRequestTime);

  // 72 h later it is due again
  g_uptimeMs += 72ull * 3600ull * 1000ull;
  up.begin(static_cast<uint32_t>(g_uptimeMs));
  runMinutes(up, 10);
  TEST_ASSERT_TRUE(t.lastRequestTime);
}

void test_not_joined_drops_window_without_backlog() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  FakeTransport t;
  t.isJoined = false;
  CollectSink storage;
  CayenneUplink up(g_cfg, g_cfg.uplinks[0].lorawan, clock, health, t);
  up.setAggregateSink(&storage);
  up.begin(0);

  runMinutes(up, 30);
  TEST_ASSERT_EQUAL(0, t.sends);
  TEST_ASSERT_EQUAL_UINT32(3, up.stats().skipped);
  TEST_ASSERT_EQUAL(18, storage.count);  // the means are still stored
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Degraded), static_cast<int>(health.overall()));

  // after joining only the current window is sent, not the three missed ones
  t.isJoined = true;
  runMinutes(up, 10);
  TEST_ASSERT_EQUAL(1, t.sends);
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Ok), static_cast<int>(health.overall()));
}

void test_failed_sensor_sends_remaining_channels() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  FakeTransport t;
  CayenneUplink up(g_cfg, g_cfg.uplinks[0].lorawan, clock, health, t);
  up.begin(0);
  for (int i = 0; i < 10; ++i) {
    g_uptimeMs += 60000;
    up.onMeasurement(raw(0, Phenomenon::AirTemperature, 20.0f));
    up.onMeasurement(raw(0, Phenomenon::RelativeHumidity, 60.0f));
    up.tick(static_cast<uint32_t>(g_uptimeMs));
  }
  TEST_ASSERT_EQUAL(1, t.sends);
  TEST_ASSERT_EQUAL(7, t.lastLen);
}

void test_empty_window_sends_nothing() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  FakeTransport t;
  CayenneUplink up(g_cfg, g_cfg.uplinks[0].lorawan, clock, health, t);
  up.begin(0);
  g_uptimeMs += 600000;
  up.tick(static_cast<uint32_t>(g_uptimeMs));
  TEST_ASSERT_EQUAL(0, t.sends);
  TEST_ASSERT_EQUAL_UINT32(1, up.stats().skipped);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sends_window_mean_every_10_minutes);
  RUN_TEST(test_time_sync_requested_only_when_due);
  RUN_TEST(test_not_joined_drops_window_without_backlog);
  RUN_TEST(test_failed_sensor_sends_remaining_channels);
  RUN_TEST(test_empty_window_sends_nothing);
  return UNITY_END();
}
