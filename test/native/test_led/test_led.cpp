#include <unity.h>

#include "Health.h"
#include "LedPattern.h"

namespace {
uint32_t fakeUptimeS() { return 0; }

using ledpattern::colorAt;
using ledpattern::fromHealth;

int color(const LedSignals& s, uint32_t nowMs) { return static_cast<int>(colorAt(s, nowMs)); }
constexpr int kGreen = static_cast<int>(LedColor::Green);
constexpr int kWhite = static_cast<int>(LedColor::White);
constexpr int kYellow = static_cast<int>(LedColor::Yellow);
constexpr int kRed = static_cast<int>(LedColor::Red);
constexpr int kPurple = static_cast<int>(LedColor::Purple);
}  // namespace

void setUp() {}
void tearDown() {}

// --- colorAt ---------------------------------------------------------------

void test_no_signals_is_steady_green() {
  const LedSignals s;
  for (uint32_t t = 0; t < 2 * ledpattern::kPeriodMs; t += 50) TEST_ASSERT_EQUAL(kGreen, color(s, t));
}

void test_single_blink_once_per_period() {
  LedSignals s;
  s.settingUp = true;
  TEST_ASSERT_EQUAL(kWhite, color(s, 0));
  TEST_ASSERT_EQUAL(kWhite, color(s, ledpattern::kBlinkMs - 1));
  TEST_ASSERT_EQUAL(kGreen, color(s, ledpattern::kBlinkMs));
  TEST_ASSERT_EQUAL(kGreen, color(s, ledpattern::kBlinkMs + ledpattern::kGapMs));  // no second blink
  TEST_ASSERT_EQUAL(kGreen, color(s, ledpattern::kPeriodMs - 1));
  TEST_ASSERT_EQUAL(kWhite, color(s, ledpattern::kPeriodMs));
}

void test_several_blinks_in_sequence() {
  LedSignals s;
  s.connectFailed = true;
  s.sensorFailed = true;
  s.storageFailed = true;
  const uint32_t step = ledpattern::kBlinkMs + ledpattern::kGapMs;
  TEST_ASSERT_EQUAL(kYellow, color(s, 0));
  TEST_ASSERT_EQUAL(kGreen, color(s, ledpattern::kBlinkMs));
  TEST_ASSERT_EQUAL(kRed, color(s, step));
  TEST_ASSERT_EQUAL(kPurple, color(s, 2 * step));
  TEST_ASSERT_EQUAL(kGreen, color(s, 3 * step));
}

void test_critical_is_steady_red() {
  LedSignals s;
  s.critical = true;
  s.settingUp = true;
  for (uint32_t t = 0; t < ledpattern::kPeriodMs; t += 50) TEST_ASSERT_EQUAL(kRed, color(s, t));
}

// --- fromHealth ------------------------------------------------------------

void test_starting_box_is_setting_up() {
  HealthRegistry h(fakeUptimeS);
  h.reportSuccess(h.add("config"));
  h.setStatus(h.add("lorawan"), ComponentStatus::Degraded);  // until joined
  h.add("sensor1:hdc1080");                                  // no reading yet
  h.reportSuccess(h.add("sd"));
  const LedSignals s = fromHealth(h);
  TEST_ASSERT_TRUE(s.settingUp);
  TEST_ASSERT_FALSE(s.connectFailed);
  TEST_ASSERT_FALSE(s.sensorFailed);
  TEST_ASSERT_FALSE(s.storageFailed);
  TEST_ASSERT_FALSE(s.critical);
}

void test_running_box_has_no_signals() {
  HealthRegistry h(fakeUptimeS);
  h.reportSuccess(h.add("config"));
  h.reportSuccess(h.add("lorawan"));
  h.reportSuccess(h.add("sensor1:hdc1080"));
  h.setStatus(h.add("sensor2:sps30"), ComponentStatus::Disabled);
  h.reportSuccess(h.add("sd"));
  h.reportError(h.add("uplink:lorawan"), 1, "no valid values in window", ComponentStatus::Degraded);
  const LedSignals s = fromHealth(h);
  TEST_ASSERT_FALSE(s.settingUp || s.connectFailed || s.sensorFailed || s.storageFailed || s.critical);
}

void test_failures_map_to_signals() {
  HealthRegistry h(fakeUptimeS);
  h.reportError(h.add("config"), 1, "no configuration stored", ComponentStatus::Degraded);
  h.reportError(h.add("lorawan"), 4, "join failed, retrying", ComponentStatus::Degraded);
  h.reportError(h.add("sensor1:hdc1080"), 2, "timeout");
  h.reportError(h.add("sd"), 1, "mount failed");
  const LedSignals s = fromHealth(h);
  TEST_ASSERT_FALSE(s.settingUp);
  TEST_ASSERT_TRUE(s.connectFailed);
  TEST_ASSERT_TRUE(s.sensorFailed);
  TEST_ASSERT_TRUE(s.storageFailed);
  TEST_ASSERT_FALSE(s.critical);  // unconfigured is not critical
}

void test_rejected_config_is_critical() {
  HealthRegistry h(fakeUptimeS);
  h.reportError(h.add("config"), 2, "bad json");
  TEST_ASSERT_TRUE(fromHealth(h).critical);
}

void test_no_data_target_does_not_blink() {
  HealthRegistry h(fakeUptimeS);
  h.reportSuccess(h.add("config"));
  h.add("lorawan");  // transport exists but never started
  h.reportSuccess(h.add("sensor1:hdc1080"));
  TEST_ASSERT_FALSE(fromHealth(h).settingUp);
  h.setStatus(h.add("lorawan"), ComponentStatus::Disabled);
  const LedSignals s = fromHealth(h);
  TEST_ASSERT_FALSE(s.settingUp || s.connectFailed);
}

void test_missing_appkey_is_connect_failed() {
  HealthRegistry h(fakeUptimeS);
  h.reportError(h.add("lorawan"), 7, "AppKey not set");
  const LedSignals s = fromHealth(h);
  TEST_ASSERT_TRUE(s.connectFailed);
  TEST_ASSERT_FALSE(s.settingUp);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_signals_is_steady_green);
  RUN_TEST(test_single_blink_once_per_period);
  RUN_TEST(test_several_blinks_in_sequence);
  RUN_TEST(test_critical_is_steady_red);
  RUN_TEST(test_starting_box_is_setting_up);
  RUN_TEST(test_running_box_has_no_signals);
  RUN_TEST(test_failures_map_to_signals);
  RUN_TEST(test_rejected_config_is_critical);
  RUN_TEST(test_no_data_target_does_not_blink);
  RUN_TEST(test_missing_appkey_is_connect_failed);
  return UNITY_END();
}
