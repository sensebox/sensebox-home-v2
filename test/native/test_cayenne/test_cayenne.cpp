#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "CayenneEncoder.h"
#include "ConfigCodec.h"

using config::LppType;

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
  FILE* f = fopen("test/fixtures/olpe.json", "rb");
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

void test_single_values() {
  uint8_t buf[8];
  bool clamped = false;

  TEST_ASSERT_EQUAL(4, cayenne::encodeValue(1, LppType::Temperature, 21.5f, buf, sizeof(buf), clamped));
  const uint8_t t[] = {0x01, 0x67, 0x00, 0xD7};  // 215
  TEST_ASSERT_EQUAL_HEX8_ARRAY(t, buf, 4);

  TEST_ASSERT_EQUAL(4, cayenne::encodeValue(1, LppType::Temperature, -5.2f, buf, sizeof(buf), clamped));
  const uint8_t tn[] = {0x01, 0x67, 0xFF, 0xCC};  // -52
  TEST_ASSERT_EQUAL_HEX8_ARRAY(tn, buf, 4);

  TEST_ASSERT_EQUAL(3, cayenne::encodeValue(2, LppType::RelativeHumidity, 55.0f, buf, sizeof(buf), clamped));
  const uint8_t h[] = {0x02, 0x68, 0x6E};  // 110 = 55 % in 0.5 % steps
  TEST_ASSERT_EQUAL_HEX8_ARRAY(h, buf, 3);

  TEST_ASSERT_EQUAL(4, cayenne::encodeValue(3, LppType::AnalogInput, 12.34f, buf, sizeof(buf), clamped));
  const uint8_t a[] = {0x03, 0x02, 0x04, 0xD2};  // 1234
  TEST_ASSERT_EQUAL_HEX8_ARRAY(a, buf, 4);
  TEST_ASSERT_FALSE(clamped);
}

void test_clamping() {
  uint8_t buf[8];
  bool clamped = false;
  // PM above 327.67 µg/m³ is clamped (decision: analog_in, plan §6 finding 5)
  TEST_ASSERT_EQUAL(4, cayenne::encodeValue(4, LppType::AnalogInput, 412.0f, buf, sizeof(buf), clamped));
  const uint8_t a[] = {0x04, 0x02, 0x7F, 0xFF};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(a, buf, 4);
  TEST_ASSERT_TRUE(clamped);

  clamped = false;
  cayenne::encodeValue(2, LppType::RelativeHumidity, 101.0f, buf, sizeof(buf), clamped);
  TEST_ASSERT_FALSE(clamped);  // 202 still fits a byte
  cayenne::encodeValue(2, LppType::RelativeHumidity, -1.0f, buf, sizeof(buf), clamped);
  TEST_ASSERT_TRUE(clamped);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);

  clamped = false;
  cayenne::encodeValue(1, LppType::Temperature, NAN, buf, sizeof(buf), clamped);
  TEST_ASSERT_TRUE(clamped);

  // too small buffer
  TEST_ASSERT_EQUAL(0, cayenne::encodeValue(1, LppType::Temperature, 1.0f, buf, 3, clamped));
}

void test_olpe_frame_layout() {
  const config::LoRaWanConfig& lw = olpeLoRaWan();
  const Measurement values[] = {
      agg(0, Phenomenon::AirTemperature, 18.3f), agg(0, Phenomenon::RelativeHumidity, 71.5f),
      agg(1, Phenomenon::PM1, 3.21f),           agg(1, Phenomenon::PM2_5, 5.5f),
      agg(1, Phenomenon::PM4, 6.0f),            agg(1, Phenomenon::PM10, 7.25f),
  };
  uint8_t frame[cayenne::kMaxPayload];
  const cayenne::EncodeResult r = cayenne::encode(lw, values, 6, frame, sizeof(frame));
  TEST_ASSERT_EQUAL(23, r.length);  // plan §6: 4 + 3 + 4 x 4 bytes
  TEST_ASSERT_EQUAL(6, r.encoded);
  TEST_ASSERT_EQUAL(0, r.missing);
  const uint8_t expected[] = {
      0x01, 0x67, 0x00, 0xB7,  // 18.3 °C
      0x02, 0x68, 0x8F,        // 71.5 %
      0x03, 0x02, 0x02, 0x26,  // PM2.5 5.50
      0x04, 0x02, 0x02, 0xD5,  // PM10 7.25
      0x05, 0x02, 0x01, 0x41,  // PM1 3.21
      0x06, 0x02, 0x02, 0x58,  // PM4 6.00
  };
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, frame, sizeof(expected));
}

void test_failed_sensor_channels_are_omitted() {
  const config::LoRaWanConfig& lw = olpeLoRaWan();
  // SPS30 in ERROR: only temperature and humidity
  const Measurement values[] = {agg(0, Phenomenon::AirTemperature, 18.3f), agg(0, Phenomenon::RelativeHumidity, 71.5f)};
  uint8_t frame[cayenne::kMaxPayload];
  const cayenne::EncodeResult r = cayenne::encode(lw, values, 2, frame, sizeof(frame));
  TEST_ASSERT_EQUAL(7, r.length);
  TEST_ASSERT_EQUAL(4, r.missing);
}

void test_frame_overflow() {
  const config::LoRaWanConfig& lw = olpeLoRaWan();
  const Measurement values[] = {
      agg(0, Phenomenon::AirTemperature, 18.3f), agg(0, Phenomenon::RelativeHumidity, 71.5f),
      agg(1, Phenomenon::PM2_5, 5.5f),
  };
  uint8_t frame[10];
  const cayenne::EncodeResult r = cayenne::encode(lw, values, 3, frame, sizeof(frame));
  TEST_ASSERT_EQUAL(7, r.length);
  TEST_ASSERT_EQUAL(1, r.overflow);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_single_values);
  RUN_TEST(test_clamping);
  RUN_TEST(test_olpe_frame_layout);
  RUN_TEST(test_failed_sensor_channels_are_omitted);
  RUN_TEST(test_frame_overflow);
  return UNITY_END();
}
