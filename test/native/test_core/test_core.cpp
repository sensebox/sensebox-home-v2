#include <string.h>
#include <unity.h>

#include "Clock.h"
#include "Log.h"
#include "Measurement.h"
#include "RingBuffer.h"
#include "Scheduling.h"

namespace {
uint64_t g_fakeUptimeMs = 0;
uint64_t fakeUptime() { return g_fakeUptimeMs; }
}  // namespace

void setUp() { g_fakeUptimeMs = 0; }
void tearDown() {}

// --- Measurement -----------------------------------------------------------

void test_phenomenon_names_roundtrip() {
  for (uint8_t i = 1; i < static_cast<uint8_t>(Phenomenon::Count); ++i) {
    const auto p = static_cast<Phenomenon>(i);
    Phenomenon parsed = Phenomenon::Unknown;
    TEST_ASSERT_TRUE(phenomenonFromName(phenomenonName(p), parsed));
    TEST_ASSERT_EQUAL(static_cast<int>(p), static_cast<int>(parsed));
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(Unit::None), static_cast<int>(defaultUnit(p)));
  }
  Phenomenon dummy;
  TEST_ASSERT_FALSE(phenomenonFromName("no_such_thing", dummy));
  TEST_ASSERT_FALSE(phenomenonFromName(nullptr, dummy));
}

// --- RingBuffer --------------------------------------------------------------

void test_ringbuffer_overwrites_oldest() {
  RingBuffer<int, 3> rb;
  TEST_ASSERT_TRUE(rb.empty());
  for (int i = 1; i <= 5; ++i) rb.push(i);
  TEST_ASSERT_EQUAL(3, rb.size());
  TEST_ASSERT_EQUAL(2, rb.overwritten());
  TEST_ASSERT_EQUAL(3, rb.at(0));
  TEST_ASSERT_EQUAL(5, rb.at(2));
  int v = 0;
  TEST_ASSERT_TRUE(rb.pop(v));
  TEST_ASSERT_EQUAL(3, v);
  TEST_ASSERT_EQUAL(2, rb.size());
}

// --- Scheduling --------------------------------------------------------------

void test_time_reached_handles_wraparound() {
  TEST_ASSERT_TRUE(timeReached(100, 100));
  TEST_ASSERT_FALSE(timeReached(99, 100));
  // due is just after the wrap, now just before it
  TEST_ASSERT_FALSE(timeReached(0xFFFFFF00u, 0x00000010u));
  TEST_ASSERT_TRUE(timeReached(0x00000020u, 0xFFFFFF00u));
  TEST_ASSERT_EQUAL_UINT32(0x110u, timeUntil(0xFFFFFF00u, 0x00000010u));
}

void test_backoff_doubles_and_caps() {
  TEST_ASSERT_EQUAL_UINT32(10000, backoffMs(10000, 600000, 0));
  TEST_ASSERT_EQUAL_UINT32(20000, backoffMs(10000, 600000, 1));
  TEST_ASSERT_EQUAL_UINT32(80000, backoffMs(10000, 600000, 3));
  TEST_ASSERT_EQUAL_UINT32(600000, backoffMs(10000, 600000, 10));
  TEST_ASSERT_EQUAL_UINT32(600000, backoffMs(10000, 600000, 255));
}

// --- Clock -------------------------------------------------------------------

void test_clock_unsynced_uses_uptime_and_flags() {
  Clock clock(fakeUptime);
  g_fakeUptimeMs = 42500;
  uint16_t q = 0;
  TEST_ASSERT_EQUAL_UINT32(42, clock.timestampNow(q));
  TEST_ASSERT_TRUE(q & quality::kTimeUnsynced);
  TEST_ASSERT_EQUAL_UINT32(0, clock.nowUtc());
  uint32_t utc = 0;
  TEST_ASSERT_FALSE(clock.toUtc(42, utc));
}

void test_clock_sync_and_backdate() {
  Clock clock(fakeUptime);
  g_fakeUptimeMs = 600000;  // 10 min after boot
  const uint64_t utcMs = 1760000000000ull;
  TEST_ASSERT_TRUE(clock.setUtc(utcMs, 600000, TimeSource::LoRaWan));
  TEST_ASSERT_TRUE(clock.isSynced());
  TEST_ASSERT_EQUAL_UINT32(1760000000u, clock.nowUtc());

  g_fakeUptimeMs = 660000;
  uint16_t q = 0;
  TEST_ASSERT_EQUAL_UINT32(1760000060u, clock.timestampNow(q));
  TEST_ASSERT_EQUAL_UINT16(0, q);

  // a measurement taken 2 min after boot, before the sync
  uint32_t utc = 0;
  TEST_ASSERT_TRUE(clock.toUtc(120, utc));
  TEST_ASSERT_EQUAL_UINT32(1760000000u - 480u, utc);
}

void test_clock_rejects_implausible_and_late_browser_time() {
  Clock clock(fakeUptime);
  TEST_ASSERT_FALSE(clock.setUtc(1000ull * 1000, 0, TimeSource::LoRaWan));  // 1970
  TEST_ASSERT_TRUE(clock.setUtc(1760000000000ull, 0, TimeSource::Browser));
  TEST_ASSERT_TRUE(clock.setUtc(1760000005000ull, 0, TimeSource::LoRaWan));
  TEST_ASSERT_FALSE(clock.setUtc(1760009999000ull, 0, TimeSource::Browser));
  TEST_ASSERT_EQUAL_UINT32(1760000005u, clock.nowUtc());
}

void test_gps_to_unix() {
  // GPS time 0 = 1980-01-06T00:00:00Z, minus 18 leap seconds applied today
  TEST_ASSERT_EQUAL_UINT64((315964800ull - 18ull) * 1000ull, Clock::gpsToUnixMs(0));
  // 2025-10-09T08:00:00Z = unix 1759996800 ; GPS = unix - 315964800 + 18
  const uint64_t gpsS = 1759996800ull - 315964800ull + 18ull;
  TEST_ASSERT_EQUAL_UINT64(1759996800000ull, Clock::gpsToUnixMs(gpsS * 1000ull));
}

// --- Logger ------------------------------------------------------------------

class CaptureSink : public LogSink {
 public:
  void write(const LogLine& line) override {
    last = line;
    ++count;
  }
  LogLine last{};
  int count = 0;
};

void test_logger_ring_levels_and_sinks() {
  logger::init(fakeUptime);
  logger::setLevel(LogLevel::Info);
  CaptureSink sink;
  TEST_ASSERT_TRUE(logger::addSink(&sink));

  g_fakeUptimeMs = 7000;
  LOGI("test", "value=%d", 42);
  LOGD("test", "hidden");
  TEST_ASSERT_EQUAL(1, sink.count);
  TEST_ASSERT_EQUAL_STRING("value=42", sink.last.msg);
  TEST_ASSERT_EQUAL_STRING("test", sink.last.tag);
  TEST_ASSERT_EQUAL_UINT32(7, sink.last.uptimeS);

  LOGW("a-very-long-module-tag", "%s", "x");
  TEST_ASSERT_EQUAL(LogLine::kTagLen - 1, strlen(sink.last.tag));

  LogLine lines[4];
  const size_t n = logger::copyRecent(lines, 4);
  TEST_ASSERT_TRUE(n >= 2);
  TEST_ASSERT_EQUAL_STRING("value=42", lines[n - 2].msg);
  LogLine one;
  TEST_ASSERT_TRUE(logger::lineAt(1, one));
  TEST_ASSERT_EQUAL_STRING("value=42", one.msg);
  TEST_ASSERT_TRUE(logger::lineAt(0, one));
  TEST_ASSERT_EQUAL_STRING("x", one.msg);
  TEST_ASSERT_FALSE(logger::lineAt(logger::lineCount(), one));
  logger::removeSink(&sink);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_phenomenon_names_roundtrip);
  RUN_TEST(test_ringbuffer_overwrites_oldest);
  RUN_TEST(test_time_reached_handles_wraparound);
  RUN_TEST(test_backoff_doubles_and_caps);
  RUN_TEST(test_clock_unsynced_uses_uptime_and_flags);
  RUN_TEST(test_clock_sync_and_backdate);
  RUN_TEST(test_clock_rejects_implausible_and_late_browser_time);
  RUN_TEST(test_gps_to_unix);
  RUN_TEST(test_logger_ring_levels_and_sinks);
  return UNITY_END();
}
