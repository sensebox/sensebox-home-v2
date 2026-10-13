#include <string.h>
#include <unity.h>

#include "Clock.h"
#include "Health.h"
#include "SensorManager.h"

// ---------------------------------------------------------------------------
// Test doubles
// ---------------------------------------------------------------------------

namespace {

uint64_t g_uptimeMs = 0;
uint64_t fakeUptimeMs() { return g_uptimeMs; }
uint32_t fakeUptimeS() { return static_cast<uint32_t>(g_uptimeMs / 1000); }

constexpr PhenomenonSpec kFakeSpecs[] = {
    {Phenomenon::AirTemperature, Unit::DegC, -40.0f, 85.0f, 0.3f, 0.01f},
    {Phenomenon::RelativeHumidity, Unit::Percent, 0.0f, 100.0f, 2.0f, 0.1f},
};

constexpr SensorDescriptor kFakeDescriptor = {
    "fake", "test", "Fake", kFakeSpecs, 2, 0x38, 1000, 500, 1000, true, "",
};

// Scriptable fake driver. Behaviour is controlled through the static `g_*` knobs below,
// because the manager creates instances itself through the registry.
struct FakeControl {
  bool presentAtInit = true;    // init succeeds
  bool measureSync = true;      // results available right after requestMeasurement()
  bool neverAnswer = false;     // measurement never finishes (timeout path)
  bool failMeasure = false;     // measurement ends in Error
  float temperature = 21.5f;
  int begins = 0;
  int resets = 0;
  int requests = 0;
};
FakeControl g_fake;

class FakeSensor : public Sensor {
 public:
  const SensorDescriptor& descriptor() const override { return kFakeDescriptor; }
  void begin(const config::SensorConfig&, uint32_t) override {
    ++g_fake.begins;
    start();
  }
  void tick(uint32_t) override {}
  bool requestMeasurement(uint32_t) override {
    if (state_ != SensorState::Ready) return false;
    ++g_fake.requests;
    if (g_fake.failMeasure) {
      state_ = SensorState::Error;
      error_ = SensorError::CommError;
      return true;
    }
    state_ = SensorState::Measuring;
    if (g_fake.measureSync && !g_fake.neverAnswer) finish();
    return true;
  }
  uint8_t takeResults(SensorReading* out, uint8_t max) override {
    if (!hasResult_ || max < 2) return 0;
    hasResult_ = false;
    out[0] = {Phenomenon::AirTemperature, g_fake.temperature, 0};
    out[1] = {Phenomenon::RelativeHumidity, 55.0f, 0};
    return 2;
  }
  SensorState state() const override { return state_; }
  SensorError lastError() const override { return error_; }
  void reset(uint32_t) override {
    ++g_fake.resets;
    start();
  }

 private:
  void start() {
    if (g_fake.presentAtInit) {
      state_ = SensorState::Ready;
      error_ = SensorError::None;
    } else {
      state_ = SensorState::Error;
      error_ = SensorError::NotFound;
    }
  }
  void finish() {
    hasResult_ = true;
    state_ = SensorState::Ready;
  }

  SensorState state_ = SensorState::Uninitialized;
  SensorError error_ = SensorError::None;
  bool hasResult_ = false;
};

Sensor* createFake() { return new FakeSensor(); }

constexpr DriverEntry kDrivers[] = {{"fake", &kFakeDescriptor, createFake}};

class CollectSink : public MeasurementSink {
 public:
  void onMeasurement(const Measurement& m) override {
    if (count < 64) items[count++] = m;
  }
  Measurement items[64];
  int count = 0;
};

config::StationConfig g_cfg;

void addSensor(uint8_t slot, const char* type, uint32_t intervalSec = 60) {
  config::SensorConfig& s = g_cfg.sensors[g_cfg.sensorCount++];
  s = config::SensorConfig{};
  s.slot = slot;
  strcpy(s.type, type);
  s.intervalSec = intervalSec;
}

// Runs the manager in 100 ms steps until `untilMs`.
void runUntil(SensorManager& mgr, uint64_t untilMs) {
  while (g_uptimeMs < untilMs) {
    g_uptimeMs += 100;
    mgr.tick(static_cast<uint32_t>(g_uptimeMs));
  }
}

}  // namespace

void setUp() {
  g_uptimeMs = 0;
  g_fake = FakeControl{};
  config::setDefaults(g_cfg);
}
void tearDown() {}

// ---------------------------------------------------------------------------

void test_measures_on_interval_and_stamps_measurements() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  CollectSink sink;
  SensorManager mgr(clock, health, sink);
  addSensor(3, "fake", 60);
  mgr.configure(g_cfg, kDrivers, 1);
  mgr.begin(0);

  runUntil(mgr, 1000);
  TEST_ASSERT_EQUAL(2, sink.count);  // first measurement right after Ready
  TEST_ASSERT_EQUAL(3, sink.items[0].sensorSlot);
  TEST_ASSERT_EQUAL(static_cast<int>(Phenomenon::AirTemperature), static_cast<int>(sink.items[0].phenomenon));
  TEST_ASSERT_EQUAL(static_cast<int>(Unit::DegC), static_cast<int>(sink.items[0].unit));
  TEST_ASSERT_EQUAL(static_cast<int>(ValueKind::Raw), static_cast<int>(sink.items[0].kind));
  TEST_ASSERT_TRUE(sink.items[0].quality & quality::kTimeUnsynced);

  runUntil(mgr, 59000);
  TEST_ASSERT_EQUAL(2, sink.count);
  runUntil(mgr, 61000);
  TEST_ASSERT_EQUAL(4, sink.count);
  runUntil(mgr, 181000);
  TEST_ASSERT_EQUAL(8, sink.count);

  SensorSlotStatus st;
  TEST_ASSERT_TRUE(mgr.status(0, static_cast<uint32_t>(g_uptimeMs), st));
  TEST_ASSERT_EQUAL_UINT32(4, st.readingCount);
  TEST_ASSERT_EQUAL_UINT32(0, st.errorCount);
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Ok), static_cast<int>(health.overall()));
}

void test_out_of_range_reading_is_flagged() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  CollectSink sink;
  SensorManager mgr(clock, health, sink);
  addSensor(0, "fake");
  g_fake.temperature = 150.0f;  // above the 85 °C physical range
  mgr.configure(g_cfg, kDrivers, 1);
  mgr.begin(0);
  runUntil(mgr, 500);
  TEST_ASSERT_TRUE(sink.count >= 1);
  TEST_ASSERT_TRUE(sink.items[0].quality & quality::kOutOfRange);
  TEST_ASSERT_FALSE(sink.items[1].quality & quality::kOutOfRange);
}

void test_missing_sensor_retries_with_backoff_and_recovers() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  CollectSink sink;
  SensorManager::Options opt;
  opt.retryBaseMs = 10000;
  opt.retryMaxMs = 40000;
  SensorManager mgr(clock, health, sink, opt);
  addSensor(0, "fake");
  g_fake.presentAtInit = false;
  mgr.configure(g_cfg, kDrivers, 1);
  mgr.begin(0);

  runUntil(mgr, 1000);
  SensorSlotStatus st;
  mgr.status(0, static_cast<uint32_t>(g_uptimeMs), st);
  TEST_ASSERT_EQUAL(static_cast<int>(SensorState::Error), static_cast<int>(st.state));
  TEST_ASSERT_EQUAL(static_cast<int>(SensorError::NotFound), static_cast<int>(st.lastError));
  TEST_ASSERT_EQUAL_UINT32(1, st.errorCount);
  TEST_ASSERT_TRUE(st.retryInMs > 8000 && st.retryInMs <= 10000);
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Error), static_cast<int>(health.overall()));

  // retries at +10 s, +20 s, +40 s, +40 s (capped)
  runUntil(mgr, 11000);
  TEST_ASSERT_EQUAL(1, g_fake.resets);
  runUntil(mgr, 31000);
  TEST_ASSERT_EQUAL(2, g_fake.resets);
  runUntil(mgr, 71000);
  TEST_ASSERT_EQUAL(3, g_fake.resets);
  runUntil(mgr, 111000);
  TEST_ASSERT_EQUAL(4, g_fake.resets);
  TEST_ASSERT_EQUAL(0, sink.count);

  // sensor plugged back in
  g_fake.presentAtInit = true;
  runUntil(mgr, 152000);
  TEST_ASSERT_EQUAL(5, g_fake.resets);
  TEST_ASSERT_EQUAL(2, sink.count);
  mgr.status(0, static_cast<uint32_t>(g_uptimeMs), st);
  TEST_ASSERT_EQUAL(static_cast<int>(SensorState::Ready), static_cast<int>(st.state));
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Ok), static_cast<int>(health.overall()));

  // backoff starts from the base again after a recovery
  g_fake.failMeasure = true;
  runUntil(mgr, 213000);
  mgr.status(0, static_cast<uint32_t>(g_uptimeMs), st);
  TEST_ASSERT_TRUE(st.retryInMs > 0 && st.retryInMs <= 10000);
}

void test_measurement_timeout_is_an_error() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  CollectSink sink;
  SensorManager mgr(clock, health, sink);
  addSensor(0, "fake");
  g_fake.neverAnswer = true;
  mgr.configure(g_cfg, kDrivers, 1);
  mgr.begin(0);
  runUntil(mgr, 1000);  // measureTimeoutMs = 500
  SensorSlotStatus st;
  mgr.status(0, static_cast<uint32_t>(g_uptimeMs), st);
  TEST_ASSERT_EQUAL(static_cast<int>(SensorError::Timeout), static_cast<int>(st.lastError));
  TEST_ASSERT_EQUAL(0, sink.count);
}

void test_failing_sensor_does_not_affect_others() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  CollectSink sink;
  SensorManager mgr(clock, health, sink);
  addSensor(0, "fake");
  addSensor(1, "does-not-exist");
  mgr.configure(g_cfg, kDrivers, 1);
  mgr.begin(0);
  runUntil(mgr, 121000);
  TEST_ASSERT_EQUAL(6, sink.count);  // 3 measurements x 2 values from slot 0

  SensorSlotStatus st;
  mgr.status(1, static_cast<uint32_t>(g_uptimeMs), st);
  TEST_ASSERT_EQUAL(static_cast<int>(SensorError::NoDriver), static_cast<int>(st.lastError));
  TEST_ASSERT_EQUAL(static_cast<int>(SensorState::Error), static_cast<int>(st.state));
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Error), static_cast<int>(health.overall()));
}

void test_disabled_sensor_is_not_started() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  CollectSink sink;
  SensorManager mgr(clock, health, sink);
  addSensor(0, "fake");
  g_cfg.sensors[0].enabled = false;
  mgr.configure(g_cfg, kDrivers, 1);
  mgr.begin(0);
  runUntil(mgr, 5000);
  TEST_ASSERT_EQUAL(0, g_fake.begins);
  TEST_ASSERT_EQUAL(0, sink.count);
  SensorSlotStatus st;
  mgr.status(0, 5000, st);
  TEST_ASSERT_EQUAL(static_cast<int>(SensorState::Disabled), static_cast<int>(st.state));
}

void test_async_measurement() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  CollectSink sink;
  SensorManager mgr(clock, health, sink);
  addSensor(0, "fake");
  g_fake.measureSync = false;
  mgr.configure(g_cfg, kDrivers, 1);
  mgr.begin(0);
  runUntil(mgr, 2000);
  // the fake never finishes asynchronously -> timeout, not a hang
  SensorSlotStatus st;
  mgr.status(0, 2000, st);
  TEST_ASSERT_EQUAL(static_cast<int>(SensorError::Timeout), static_cast<int>(st.lastError));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_measures_on_interval_and_stamps_measurements);
  RUN_TEST(test_out_of_range_reading_is_flagged);
  RUN_TEST(test_missing_sensor_retries_with_backoff_and_recovers);
  RUN_TEST(test_measurement_timeout_is_an_error);
  RUN_TEST(test_failing_sensor_does_not_affect_others);
  RUN_TEST(test_disabled_sensor_is_not_started);
  RUN_TEST(test_async_measurement);
  return UNITY_END();
}
