#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "LoRaWanUplink.h"
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

config::StationConfig g_compactCfg;

void loadConfig(const char* path, config::StationConfig& cfg) {
  static char json[8192];
  FILE* f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL(f);
  const size_t n = fread(json, 1, sizeof(json) - 1, f);
  fclose(f);
  const config::DecodeResult r = config::decode(json, n, cfg);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
}

// Cayenne: test/fixtures/olpe.json. Compact: the Olpe example config.
void loadOlpe() {
  loadConfig("test/fixtures/olpe.json", g_cfg);
  loadConfig("config/examples/olpe.json", g_compactCfg);
  TEST_ASSERT_EQUAL(static_cast<int>(config::UplinkType::LoRaWanCompact), static_cast<int>(g_compactCfg.uplinks[0].type));
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
void feedMinute(LoRaWanUplink& up, float temp) {
  up.onMeasurement(raw(0, Phenomenon::AirTemperature, temp));
  up.onMeasurement(raw(0, Phenomenon::RelativeHumidity, 60.0f));
  up.onMeasurement(raw(1, Phenomenon::PM1, 1.0f));
  up.onMeasurement(raw(1, Phenomenon::PM2_5, 2.0f));
  up.onMeasurement(raw(1, Phenomenon::PM4, 3.0f));
  up.onMeasurement(raw(1, Phenomenon::PM10, 4.0f));
}

void runMinutes(LoRaWanUplink& up, int minutes, float temp = 20.0f) {
  for (int i = 0; i < minutes; ++i) {
    g_uptimeMs += 60000;
    feedMinute(up, temp);
    up.tick(static_cast<uint32_t>(g_uptimeMs));
  }
}

// A full 10 minutes (XX:X0:00) in UTC milliseconds.
constexpr uint64_t kGridUtcMs = 1760000400000ull;

// Runs in 1 s steps until `untilMs`, with one measurement of both sensors every full minute of
// uptime. `boundaryTemp` is used for the measurement at uptime `boundaryMs`.
void runSeconds(LoRaWanUplink& up, uint64_t untilMs, uint64_t boundaryMs = 0, float boundaryTemp = 20.0f) {
  while (g_uptimeMs < untilMs) {
    g_uptimeMs += 1000;
    if (g_uptimeMs % 60000 == 0) feedMinute(up, g_uptimeMs == boundaryMs ? boundaryTemp : 20.0f);
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
  LoRaWanUplink up(g_cfg, g_cfg.uplinks[0].type, g_cfg.uplinks[0].lorawan, clock, health, t);
  up.setSendDelayMs(0);
  up.setAggregateSink(&storage);
  up.begin(0);

  runMinutes(up, 9);
  TEST_ASSERT_EQUAL(0, t.sends);
  runMinutes(up, 1, 20.0f);
  TEST_ASSERT_EQUAL(1, t.sends);
  TEST_ASSERT_EQUAL(cayenne::kPort, t.lastPort);
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
  LoRaWanUplink up(g_cfg, g_cfg.uplinks[0].type, g_cfg.uplinks[0].lorawan, clock, health, t);
  up.setSendDelayMs(0);
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
  LoRaWanUplink up(g_cfg, g_cfg.uplinks[0].type, g_cfg.uplinks[0].lorawan, clock, health, t);
  up.setSendDelayMs(0);
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
  LoRaWanUplink up(g_cfg, g_cfg.uplinks[0].type, g_cfg.uplinks[0].lorawan, clock, health, t);
  up.setSendDelayMs(0);
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
  LoRaWanUplink up(g_cfg, g_cfg.uplinks[0].type, g_cfg.uplinks[0].lorawan, clock, health, t);
  up.setSendDelayMs(0);
  up.begin(0);
  g_uptimeMs += 600000;
  up.tick(static_cast<uint32_t>(g_uptimeMs));
  TEST_ASSERT_EQUAL(0, t.sends);
  TEST_ASSERT_EQUAL_UINT32(1, up.stats().skipped);
}

void test_compact_window_on_utc_grid_with_time() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  FakeTransport t;
  CollectSink storage;
  LoRaWanUplink up(g_compactCfg, g_compactCfg.uplinks[0].type, g_compactCfg.uplinks[0].lorawan, clock, health, t);
  up.setAggregateSink(&storage);
  up.setSendDelayMs(30000);
  // 3 min into a window: it ends at the next full 10 minutes, uptime 420 s.
  TEST_ASSERT_TRUE(clock.setUtc(kGridUtcMs + 180000, 0, TimeSource::LoRaWan));
  up.begin(0);

  // The measurement exactly on the boundary (40 °C) still belongs to the window.
  runSeconds(up, 424000, 420000, 40.0f);
  TEST_ASSERT_EQUAL_UINT32(0, up.stats().windows);
  runSeconds(up, 425000);
  TEST_ASSERT_EQUAL_UINT32(1, up.stats().windows);
  const uint32_t windowEnd = static_cast<uint32_t>(kGridUtcMs / 1000) + 600;
  TEST_ASSERT_EQUAL_UINT32(windowEnd, storage.last.timestamp);
  TEST_ASSERT_FALSE(storage.last.quality & quality::kTimeUnsynced);

  // Sent with the per-box delay on port 2, stamped with the window end, not the send time.
  runSeconds(up, 454000);
  TEST_ASSERT_EQUAL(0, t.sends);
  runSeconds(up, 455000);
  TEST_ASSERT_EQUAL(1, t.sends);
  TEST_ASSERT_EQUAL(compact::kPort, t.lastPort);
  TEST_ASSERT_EQUAL(15, t.lastLen);
  const uint8_t frame[] = {static_cast<uint8_t>(windowEnd >> 24),
                           static_cast<uint8_t>(windowEnd >> 16),
                           static_cast<uint8_t>(windowEnd >> 8),
                           static_cast<uint8_t>(windowEnd),
                           0x08, 0xEE,  // samples at 1..7 min of uptime: (6 x 20 + 40) / 7 = 22.86 °C
                           0x78,        // 60 %
                           0x00, 0x14,  // PM2.5 2.0
                           0x00, 0x28,  // PM10 4.0
                           0x00, 0x0A,  // PM1 1.0
                           0x00, 0x1E}; // PM4 3.0
  TEST_ASSERT_EQUAL_HEX8_ARRAY(frame, t.last, sizeof(frame));

  // The next window ends 10 min later.
  runSeconds(up, 1055000);
  TEST_ASSERT_EQUAL(2, t.sends);
  TEST_ASSERT_EQUAL_UINT32(windowEnd + 600, storage.last.timestamp);
}

void test_first_sync_moves_window_onto_grid() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  FakeTransport t;
  CollectSink storage;
  LoRaWanUplink up(g_compactCfg, g_compactCfg.uplinks[0].type, g_compactCfg.uplinks[0].lorawan, clock, health, t);
  up.setAggregateSink(&storage);
  up.setSendDelayMs(0);
  up.begin(0);

  // Unsynced: the time field is 0.
  runSeconds(up, 600000);
  TEST_ASSERT_EQUAL(1, t.sends);
  TEST_ASSERT_EQUAL(15, t.lastLen);
  const uint8_t noTime[] = {0, 0, 0, 0};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(noTime, t.last, 4);

  // Synced 8 min into a grid window: 2 min left is too short, so it is merged into the next one.
  TEST_ASSERT_TRUE(clock.setUtc(kGridUtcMs + 480000, g_uptimeMs, TimeSource::LoRaWan));
  runSeconds(up, 600000 + 724000);
  TEST_ASSERT_EQUAL(1, t.sends);
  runSeconds(up, 600000 + 725000);
  TEST_ASSERT_EQUAL(2, t.sends);
  const uint32_t windowEnd = static_cast<uint32_t>(kGridUtcMs / 1000) + 1200;
  TEST_ASSERT_EQUAL_UINT32(windowEnd, storage.last.timestamp);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(windowEnd), t.last[3]);
}

void test_compact_sensor_failure_sends_missing_markers() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  FakeTransport t;
  LoRaWanUplink up(g_compactCfg, g_compactCfg.uplinks[0].type, g_compactCfg.uplinks[0].lorawan, clock, health, t);
  up.setSendDelayMs(0);
  up.begin(0);
  for (int i = 0; i < 10; ++i) {
    g_uptimeMs += 60000;
    up.onMeasurement(raw(1, Phenomenon::PM2_5, 2.0f));
    up.onMeasurement(raw(1, Phenomenon::PM10, 4.0f));
    up.onMeasurement(raw(1, Phenomenon::PM1, 1.0f));
    up.onMeasurement(raw(1, Phenomenon::PM4, 3.0f));
    up.tick(static_cast<uint32_t>(g_uptimeMs));
  }
  TEST_ASSERT_EQUAL(1, t.sends);
  TEST_ASSERT_EQUAL(15, t.lastLen);  // fixed layout: the AM2315C values are marked missing
  const uint8_t values[] = {0x80, 0x00, 0xFF, 0x00, 0x14};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(values, t.last + 4, sizeof(values));
}

void test_send_delay_depends_on_dev_eui() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  FakeTransport t;
  config::LoRaWanConfig a = g_cfg.uplinks[0].lorawan;
  config::LoRaWanConfig b = a;
  strcpy(a.devEui, "70B3D57ED0000001");
  strcpy(b.devEui, "70B3D57ED0000002");
  LoRaWanUplink upA(g_cfg, g_cfg.uplinks[0].type, a, clock, health, t);
  LoRaWanUplink upB(g_cfg, g_cfg.uplinks[0].type, b, clock, health, t);
  TEST_ASSERT_TRUE(upA.sendDelayMs() < LoRaWanUplink::kMaxSendDelayMs);
  TEST_ASSERT_TRUE(upB.sendDelayMs() < LoRaWanUplink::kMaxSendDelayMs);
  TEST_ASSERT_TRUE(upA.sendDelayMs() != upB.sendDelayMs());

  // Capped at a quarter of short intervals.
  a.intervalSec = 120;
  LoRaWanUplink upShort(g_cfg, g_cfg.uplinks[0].type, a, clock, health, t);
  TEST_ASSERT_TRUE(upShort.sendDelayMs() < 30000);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sends_window_mean_every_10_minutes);
  RUN_TEST(test_time_sync_requested_only_when_due);
  RUN_TEST(test_not_joined_drops_window_without_backlog);
  RUN_TEST(test_failed_sensor_sends_remaining_channels);
  RUN_TEST(test_empty_window_sends_nothing);
  RUN_TEST(test_compact_window_on_utc_grid_with_time);
  RUN_TEST(test_first_sync_moves_window_onto_grid);
  RUN_TEST(test_compact_sensor_failure_sends_missing_markers);
  RUN_TEST(test_send_delay_depends_on_dev_eui);
  return UNITY_END();
}
