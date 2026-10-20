#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "CompactEncoder.h"
#include "ConfigCodec.h"

namespace {

Measurement agg(uint8_t slot, Phenomenon p, float v) {
  Measurement m{};
  m.timestamp = 1760000000u;
  m.value = v;
  m.sensorSlot = slot;
  m.phenomenon = p;
  m.unit = defaultUnit(p);
  m.kind = ValueKind::Aggregated;
  return m;
}

config::StationConfig g_cfg;

const config::LoRaWanConfig& olpeLoRaWan() {
  static char json[8192];
  FILE* f = fopen("config/examples/olpe.json", "rb");
  TEST_ASSERT_NOT_NULL(f);
  const size_t n = fread(json, 1, sizeof(json) - 1, f);
  fclose(f);
  const config::DecodeResult r = config::decode(json, n, g_cfg);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
  return g_cfg.uplinks[0].lorawan;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_olpe_frame_layout() {
  const config::LoRaWanConfig& lw = olpeLoRaWan();
  TEST_ASSERT_EQUAL(15, compact::frameSize(lw));
  const Measurement values[] = {
      agg(1, Phenomenon::PM4, 3.0f),  // order in `values` doesn't matter, the config's does
      agg(0, Phenomenon::AirTemperature, 22.86f),
      agg(0, Phenomenon::RelativeHumidity, 60.4f),
      agg(1, Phenomenon::PM2_5, 12.34f),
      agg(1, Phenomenon::PM10, 4.0f),
      agg(1, Phenomenon::PM1, 1.0f),
  };
  uint8_t out[32];
  const compact::EncodeResult r = compact::encode(lw, 0x68E6AB90u, values, 6, out, sizeof(out));
  TEST_ASSERT_EQUAL(15, r.length);
  TEST_ASSERT_EQUAL(6, r.encoded);
  TEST_ASSERT_EQUAL(0, r.missing);
  const uint8_t expected[] = {0x68, 0xE6, 0xAB, 0x90,  // time
                              0x08, 0xEE,              // 22.86 °C
                              0x79,                    // 60.4 % -> 60.5 % (121)
                              0x00, 0x7B,              // PM2.5 12.3
                              0x00, 0x28,              // PM10 4.0
                              0x00, 0x0A,              // PM1 1.0
                              0x00, 0x1E};             // PM4 3.0
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof(expected));
}

void test_missing_values_and_clamping() {
  const config::LoRaWanConfig& lw = olpeLoRaWan();
  const Measurement values[] = {
      agg(0, Phenomenon::AirTemperature, -12.34f),
      agg(0, Phenomenon::RelativeHumidity, NAN),
      agg(1, Phenomenon::PM2_5, 7000.0f),
      agg(1, Phenomenon::PM10, -1.0f),
  };
  uint8_t out[32];
  const compact::EncodeResult r = compact::encode(lw, 0, values, 4, out, sizeof(out));
  TEST_ASSERT_EQUAL(15, r.length);
  TEST_ASSERT_EQUAL(3, r.encoded);
  TEST_ASSERT_EQUAL(3, r.missing);  // NaN humidity, no PM1, no PM4
  TEST_ASSERT_EQUAL(2, r.clamped);
  const uint8_t expected[] = {0x00, 0x00, 0x00, 0x00,  // no time yet
                              0xFB, 0x2E,              // -12.34 °C
                              0xFF,                    // missing
                              0xFF, 0xFE,              // 7000 clamped to 6553.4, never the marker
                              0x00, 0x00,              // -1 clamped to 0
                              0xFF, 0xFF,              // missing
                              0xFF, 0xFF};             // missing
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, sizeof(expected));

  // A temperature beyond the range never becomes the 0x8000 marker.
  const Measurement cold[] = {agg(0, Phenomenon::AirTemperature, -400.0f)};
  compact::encode(lw, 0, cold, 1, out, sizeof(out));
  TEST_ASSERT_EQUAL_HEX8(0x80, out[4]);
  TEST_ASSERT_EQUAL_HEX8(0x01, out[5]);
}

void test_buffer_too_small() {
  const config::LoRaWanConfig& lw = olpeLoRaWan();
  uint8_t out[14];
  TEST_ASSERT_EQUAL(0, compact::encode(lw, 1u, nullptr, 0, out, sizeof(out)).length);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_olpe_frame_layout);
  RUN_TEST(test_missing_values_and_clamping);
  RUN_TEST(test_buffer_too_small);
  return UNITY_END();
}
