#include <unity.h>

#include "Airtime.h"
#include "ButtonDetector.h"

void setUp() {}
void tearDown() {}

void test_airtime_matches_reference_values() {
  // 23 B Cayenne payload (Olpe frame) = 36 B PHY payload (Semtech AN1200.13):
  // SF7 77.1 ms, SF8 143.9 ms, SF9 267.3 ms, SF12 1974.3 ms (with low data rate optimisation)
  TEST_ASSERT_UINT32_WITHIN(1, 78, airtime::uplinkMs(7, 23));
  TEST_ASSERT_UINT32_WITHIN(1, 144, airtime::uplinkMs(8, 23));
  TEST_ASSERT_UINT32_WITHIN(1, 268, airtime::uplinkMs(9, 23));
  TEST_ASSERT_UINT32_WITHIN(2, 1975, airtime::uplinkMs(12, 23));
  TEST_ASSERT_EQUAL_UINT32(0, airtime::uplinkMs(6, 23));
  // longer payload never needs less airtime
  TEST_ASSERT_TRUE(airtime::uplinkMs(7, 24) >= airtime::uplinkMs(7, 23));
}

void test_budget_limits_and_refills() {
  airtime::Budget b;  // 30 s per day
  const uint32_t frame = airtime::uplinkMs(9, 23);
  uint32_t now = 1000;
  int sent = 0;
  // Steady state: start with the initial full bucket used up, then every 10 min at SF9 for a day.
  b.spend(airtime::Budget::kDailyMs, now);
  for (int i = 0; i < 144; ++i, now += 600000) {
    if (b.canSpend(frame, now)) {
      b.spend(frame, now);
      ++sent;
    }
  }
  // 10-min interval doesn't fit SF9 (~13 min would): the budget stretches it to ~112 per day
  TEST_ASSERT_TRUE(sent < 144);
  TEST_ASSERT_INT_WITHIN(3, 112, sent);

  airtime::Budget b7;
  const uint32_t frame7 = airtime::uplinkMs(7, 23);
  sent = 0;
  b7.spend(airtime::Budget::kDailyMs, now);
  for (int i = 0; i < 144; ++i, now += 600000) {
    if (b7.canSpend(frame7, now)) {
      b7.spend(frame7, now);
      ++sent;
    }
  }
  TEST_ASSERT_INT_WITHIN(1, 144, sent);  // SF7 fits easily (the very first frame waits for a refill)
}

void test_budget_goes_negative_after_join_burst() {
  airtime::Budget b;
  b.spend(40000, 0);
  TEST_ASSERT_TRUE(b.availableMs(0) < 0);
  TEST_ASSERT_FALSE(b.canSpend(100, 1000));
  // 12 h later half a day of budget has come back
  TEST_ASSERT_TRUE(b.canSpend(100, 12u * 3600u * 1000u));
}

void test_button_short_and_long_press() {
  ButtonDetector btn(3000, 30);
  uint32_t t = 0;
  // bounce
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonDetector::Event::None), static_cast<int>(btn.update(true, t += 5)));
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonDetector::Event::None), static_cast<int>(btn.update(false, t += 5)));
  TEST_ASSERT_FALSE(btn.pressed());

  // short press
  btn.update(true, t += 10);
  btn.update(true, t += 50);
  TEST_ASSERT_TRUE(btn.pressed());
  btn.update(false, t += 500);
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonDetector::Event::ShortPress), static_cast<int>(btn.update(false, t += 50)));

  // long press fires once while held, no short press on release
  btn.update(true, t += 100);
  ButtonDetector::Event e = ButtonDetector::Event::None;
  int longs = 0;
  for (int i = 0; i < 400; ++i) {
    e = btn.update(true, t += 10);
    if (e == ButtonDetector::Event::LongPress) ++longs;
  }
  TEST_ASSERT_EQUAL(1, longs);
  btn.update(false, t += 10);
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonDetector::Event::None), static_cast<int>(btn.update(false, t += 50)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_airtime_matches_reference_values);
  RUN_TEST(test_budget_limits_and_refills);
  RUN_TEST(test_budget_goes_negative_after_join_burst);
  RUN_TEST(test_button_short_and_long_press);
  return UNITY_END();
}
