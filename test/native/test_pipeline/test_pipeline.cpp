#include <math.h>
#include <string.h>
#include <unity.h>

#include "Aggregator.h"
#include "BootGuard.h"
#include "Health.h"
#include "Pipeline.h"
#include "QualityChecker.h"

namespace {

class CollectSink : public MeasurementSink {
 public:
  void onMeasurement(const Measurement& m) override {
    if (count < 64) items[count++] = m;
  }
  const Measurement* find(uint8_t slot, Phenomenon p, ValueKind k) const {
    for (int i = 0; i < count; ++i) {
      if (items[i].sensorSlot == slot && items[i].phenomenon == p && items[i].kind == k) return &items[i];
    }
    return nullptr;
  }
  Measurement items[64];
  int count = 0;
};

Measurement make(uint8_t slot, Phenomenon p, float v, uint32_t ts, ValueKind k = ValueKind::Raw, uint16_t q = 0) {
  Measurement m{};
  m.timestamp = ts;
  m.value = v;
  m.sensorSlot = slot;
  m.phenomenon = p;
  m.unit = defaultUnit(p);
  m.kind = k;
  m.quality = q;
  return m;
}

uint32_t g_uptimeS = 0;
uint32_t fakeUptimeS() { return g_uptimeS; }

config::StationConfig g_cfg;

}  // namespace

void setUp() { config::setDefaults(g_cfg); }
void tearDown() {}

// --- QualityChecker ----------------------------------------------------------

void test_quality_implausible() {
  QualityChecker qc;
  TEST_ASSERT_EQUAL_UINT16(0, qc.check(make(0, Phenomenon::AirTemperature, 20.0f, 1000)));
  TEST_ASSERT_TRUE(qc.check(make(0, Phenomenon::AirTemperature, 75.0f, 1060)) & quality::kImplausible);
  TEST_ASSERT_TRUE(qc.check(make(0, Phenomenon::RelativeHumidity, NAN, 1000)) & quality::kImplausible);
  TEST_ASSERT_TRUE(qc.check(make(0, Phenomenon::PM10, -1.0f, 1000)) & quality::kImplausible);
}

void test_quality_spike_rate_per_minute() {
  QualityChecker qc;
  qc.check(make(0, Phenomenon::AirTemperature, 20.0f, 1000));
  // +4 °C in one minute: allowed (limit 5 °C/min)
  TEST_ASSERT_FALSE(qc.check(make(0, Phenomenon::AirTemperature, 24.0f, 1060)) & quality::kSpike);
  // +8 °C in one minute: spike
  TEST_ASSERT_TRUE(qc.check(make(0, Phenomenon::AirTemperature, 32.0f, 1120)) & quality::kSpike);
  // +8 °C over 10 minutes: fine
  TEST_ASSERT_FALSE(qc.check(make(0, Phenomenon::AirTemperature, 40.0f, 1720)) & quality::kSpike);
  // PM has no spike check
  qc.check(make(1, Phenomenon::PM10, 5.0f, 1000));
  TEST_ASSERT_FALSE(qc.check(make(1, Phenomenon::PM10, 500.0f, 1060)) & quality::kSpike);
}

void test_quality_stuck_but_not_saturated() {
  QualityChecker qc;
  uint16_t flags = 0;
  for (int i = 0; i < 29; ++i) flags = qc.check(make(0, Phenomenon::AirTemperature, 12.34f, 1000 + i * 60));
  TEST_ASSERT_FALSE(flags & quality::kStuck);
  flags = qc.check(make(0, Phenomenon::AirTemperature, 12.34f, 1000 + 29 * 60));
  TEST_ASSERT_TRUE(flags & quality::kStuck);
  flags = qc.check(make(0, Phenomenon::AirTemperature, 12.35f, 1000 + 30 * 60));
  TEST_ASSERT_FALSE(flags & quality::kStuck);

  // 100 % RH in fog for hours is saturation, not a stuck sensor
  for (int i = 0; i < 50; ++i) flags = qc.check(make(0, Phenomenon::RelativeHumidity, 100.0f, 1000 + i * 60));
  TEST_ASSERT_FALSE(flags & quality::kStuck);
  // 0 µg/m³ in clean air likewise
  for (int i = 0; i < 50; ++i) flags = qc.check(make(1, Phenomenon::PM1, 0.0f, 1000 + i * 60));
  TEST_ASSERT_FALSE(flags & quality::kStuck);
}

// --- Pipeline -----------------------------------------------------------------

void test_pipeline_flags_and_calibrates() {
  config::SensorConfig& s = g_cfg.sensors[g_cfg.sensorCount++];
  s.slot = 0;
  strcpy(s.type, "am2315c");
  s.calibrations[0] = {Phenomenon::AirTemperature, 1.0f, -0.5f};
  s.calibrationCount = 1;

  Pipeline pipeline(g_cfg);
  CollectSink sink;
  TEST_ASSERT_TRUE(pipeline.addSink(&sink));

  pipeline.onMeasurement(make(0, Phenomenon::AirTemperature, 20.0f, 1000));
  pipeline.onMeasurement(make(0, Phenomenon::RelativeHumidity, 50.0f, 1000));
  TEST_ASSERT_EQUAL(3, sink.count);  // raw temp, corrected temp, raw humidity

  const Measurement* raw = sink.find(0, Phenomenon::AirTemperature, ValueKind::Raw);
  const Measurement* cor = sink.find(0, Phenomenon::AirTemperature, ValueKind::Corrected);
  TEST_ASSERT_NOT_NULL(raw);
  TEST_ASSERT_NOT_NULL(cor);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, raw->value);  // raw value is never modified
  TEST_ASSERT_EQUAL_FLOAT(19.5f, cor->value);
  TEST_ASSERT_NULL(sink.find(0, Phenomenon::RelativeHumidity, ValueKind::Corrected));

  pipeline.onMeasurement(make(0, Phenomenon::RelativeHumidity, 150.0f, 1060));
  TEST_ASSERT_TRUE(sink.items[sink.count - 1].quality & quality::kImplausible);
}

// --- Aggregator ---------------------------------------------------------------

void test_aggregator_mean_excludes_flagged() {
  Aggregator agg;
  agg.setExpectedSamples(1, 4);
  agg.add(make(1, Phenomenon::PM2_5, 10.0f, 60));
  agg.add(make(1, Phenomenon::PM2_5, 20.0f, 120));
  agg.add(make(1, Phenomenon::PM2_5, 999.0f, 180, ValueKind::Raw, quality::kSpike));
  agg.add(make(1, Phenomenon::PM2_5, 30.0f, 240, ValueKind::Raw, quality::kTimeUnsynced));
  agg.add(make(1, Phenomenon::PM10, 40.0f, 240));

  CollectSink sink;
  TEST_ASSERT_EQUAL(2, agg.flush(1760000000u, 0, sink));
  const Measurement* pm25 = sink.find(1, Phenomenon::PM2_5, ValueKind::Aggregated);
  TEST_ASSERT_NOT_NULL(pm25);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, pm25->value);
  TEST_ASSERT_EQUAL_UINT32(1760000000u, pm25->timestamp);
  // 3 of 4 expected samples = 75 %: complete. Sample time flags don't leak into the window.
  TEST_ASSERT_FALSE(pm25->quality & quality::kIncompleteAgg);
  TEST_ASSERT_FALSE(pm25->quality & quality::kTimeUnsynced);
  const Measurement* pm10 = sink.find(1, Phenomenon::PM10, ValueKind::Aggregated);
  TEST_ASSERT_TRUE(pm10->quality & quality::kIncompleteAgg);  // 1 of 4

  // new window starts empty
  CollectSink sink2;
  TEST_ASSERT_EQUAL(0, agg.flush(1760000600u, 0, sink2));
}

void test_aggregator_prefers_corrected_series() {
  Aggregator agg;
  agg.add(make(0, Phenomenon::AirTemperature, 20.0f, 60, ValueKind::Raw));
  agg.add(make(0, Phenomenon::AirTemperature, 19.5f, 60, ValueKind::Corrected));
  agg.add(make(0, Phenomenon::RelativeHumidity, 50.0f, 60, ValueKind::Raw));
  CollectSink sink;
  TEST_ASSERT_EQUAL(2, agg.flush(600, quality::kTimeUnsynced, sink));
  const Measurement* t = sink.find(0, Phenomenon::AirTemperature, ValueKind::Aggregated);
  TEST_ASSERT_EQUAL_FLOAT(19.5f, t->value);
  TEST_ASSERT_TRUE(t->quality & quality::kTimeUnsynced);
}

void test_aggregator_all_samples_flagged_emits_nothing() {
  Aggregator agg;
  agg.add(make(1, Phenomenon::PM10, 5.0f, 60, ValueKind::Raw, quality::kWarmup));
  CollectSink sink;
  TEST_ASSERT_EQUAL(0, agg.flush(600, 0, sink));
}

// --- Health / BootGuard ----------------------------------------------------------

void test_health_registry() {
  HealthRegistry health(fakeUptimeS);
  const auto a = health.add("lorawan");
  const auto b = health.add("sd");
  TEST_ASSERT_EQUAL(a, health.add("lorawan"));
  health.reportSuccess(a, 1760000000u);
  health.setStatus(b, ComponentStatus::Degraded);
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Degraded), static_cast<int>(health.overall()));
  g_uptimeS = 77;
  health.reportError(a, -3, "join failed");
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Error), static_cast<int>(health.overall()));
  ComponentHealth h;
  TEST_ASSERT_TRUE(health.get(a, h));
  TEST_ASSERT_EQUAL_UINT32(1, h.errorCount);
  TEST_ASSERT_EQUAL_STRING("join failed", h.lastError);
  TEST_ASSERT_EQUAL_UINT32(77, h.lastErrorUptimeS);
  TEST_ASSERT_EQUAL_UINT32(1760000000u, h.lastSuccessUtc);
  TEST_ASSERT_FALSE(health.get(42, h));
}

void test_boot_guard_enters_safe_mode_after_crash_loop() {
  uint8_t persisted = 0;
  for (int boot = 1; boot <= BootGuard::kMaxUnhealthyBoots; ++boot) {
    BootGuard g;
    persisted = g.onBoot(persisted, true);
    TEST_ASSERT_FALSE(g.safeMode());
  }
  BootGuard g;
  persisted = g.onBoot(persisted, true);
  TEST_ASSERT_TRUE(g.safeMode());

  // a healthy run resets the counter
  TEST_ASSERT_FALSE(g.tick(BootGuard::kHealthyAfterMs - 1));
  TEST_ASSERT_TRUE(g.tick(BootGuard::kHealthyAfterMs));
  TEST_ASSERT_FALSE(g.tick(BootGuard::kHealthyAfterMs + 1000));
  TEST_ASSERT_EQUAL(0, g.unhealthyBoots());
}

void test_boot_guard_normal_start_ends_safe_mode() {
  uint8_t persisted = 0;
  for (int boot = 0; boot <= BootGuard::kMaxUnhealthyBoots; ++boot) {
    BootGuard g;
    persisted = g.onBoot(persisted, true);
  }
  BootGuard crashed;
  TEST_ASSERT_TRUE((crashed.onBoot(persisted, true), crashed.safeMode()));

  // power-on, flashing or the console 'reboot': normal start, counter cleared
  BootGuard normal;
  persisted = normal.onBoot(persisted, false);
  TEST_ASSERT_FALSE(normal.safeMode());
  TEST_ASSERT_EQUAL(0, persisted);

  // many normal restarts in a row never lead to safe mode
  for (int boot = 0; boot < 20; ++boot) {
    BootGuard g;
    persisted = g.onBoot(persisted, false);
    TEST_ASSERT_FALSE(g.safeMode());
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_quality_implausible);
  RUN_TEST(test_quality_spike_rate_per_minute);
  RUN_TEST(test_quality_stuck_but_not_saturated);
  RUN_TEST(test_pipeline_flags_and_calibrates);
  RUN_TEST(test_aggregator_mean_excludes_flagged);
  RUN_TEST(test_aggregator_prefers_corrected_series);
  RUN_TEST(test_aggregator_all_samples_flagged_emits_nothing);
  RUN_TEST(test_health_registry);
  RUN_TEST(test_boot_guard_enters_safe_mode_after_crash_loop);
  RUN_TEST(test_boot_guard_normal_start_ends_safe_mode);
  return UNITY_END();
}
