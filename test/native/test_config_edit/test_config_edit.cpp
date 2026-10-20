#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "ConfigCodec.h"
#include "ConfigEdit.h"

using namespace config;

namespace {

StationConfig g_cfg;
char g_err[96];

void loadOlpe() {
  static char json[8192];
  FILE* f = fopen("test/fixtures/olpe.json", "rb");
  TEST_ASSERT_NOT_NULL(f);
  const size_t n = fread(json, 1, sizeof(json) - 1, f);
  fclose(f);
  const DecodeResult r = decode(json, n, g_cfg);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
}

}  // namespace

void setUp() {
  loadOlpe();
  g_err[0] = '\0';
}
void tearDown() {}

void test_field_paths() {
  TEST_ASSERT_EQUAL(static_cast<int>(Field::Name), static_cast<int>(fieldFromPath(nullptr, "name")));
  TEST_ASSERT_EQUAL(static_cast<int>(Field::Name), static_cast<int>(fieldFromPath("station", "name")));
  TEST_ASSERT_EQUAL(static_cast<int>(Field::LoRaWanDevEui), static_cast<int>(fieldFromPath("uplinks", "devEUI")));
  TEST_ASSERT_EQUAL(static_cast<int>(Field::LoRaWanDevEui), static_cast<int>(fieldFromPath("uplinks", "deveui")));
  TEST_ASSERT_EQUAL(static_cast<int>(Field::LoRaWanJoinEui), static_cast<int>(fieldFromPath("uplinks", "joinEui")));
  TEST_ASSERT_EQUAL(static_cast<int>(Field::LoRaWanJoinEui), static_cast<int>(fieldFromPath("uplinks", "appEUI")));
  TEST_ASSERT_EQUAL(static_cast<int>(Field::Unknown), static_cast<int>(fieldFromPath(nullptr, "uplinks")));
  TEST_ASSERT_EQUAL(static_cast<int>(Field::Unknown), static_cast<int>(fieldFromPath("uplinks", "appKey")));
  TEST_ASSERT_EQUAL(static_cast<int>(Field::Unknown), static_cast<int>(fieldFromPath("uplinks", nullptr)));
}

void test_set_name() {
  TEST_ASSERT_TRUE(setField(g_cfg, Field::Name, "Olpe 017 Rathaus", g_err, sizeof(g_err)));
  TEST_ASSERT_EQUAL_STRING("Olpe 017 Rathaus", g_cfg.name);
  TEST_ASSERT_FALSE(setField(g_cfg, Field::Name, "", g_err, sizeof(g_err)));
  TEST_ASSERT_FALSE(setField(g_cfg, Field::Name, "0123456789012345678901234567890123456789", g_err, sizeof(g_err)));
  TEST_ASSERT_EQUAL_STRING("Olpe 017 Rathaus", g_cfg.name);  // unchanged after a failed edit
}

void test_set_euis_normalises_input() {
  TEST_ASSERT_TRUE(setField(g_cfg, Field::LoRaWanDevEui, "70b3d57ed0001234", g_err, sizeof(g_err)));
  TEST_ASSERT_EQUAL_STRING("70B3D57ED0001234", g_cfg.uplinks[0].lorawan.devEui);
  TEST_ASSERT_TRUE(setField(g_cfg, Field::LoRaWanJoinEui, "00 00 00 00 00 00 00 01", g_err, sizeof(g_err)));
  TEST_ASSERT_EQUAL_STRING("0000000000000001", g_cfg.uplinks[0].lorawan.joinEui);
  TEST_ASSERT_TRUE(setField(g_cfg, Field::LoRaWanJoinEui, "00:00:00:00:00:00:00:02", g_err, sizeof(g_err)));
  TEST_ASSERT_EQUAL_STRING("0000000000000002", g_cfg.uplinks[0].lorawan.joinEui);
  TEST_ASSERT_EQUAL_STRING("70B3D57ED0001234", g_cfg.uplinks[0].lorawan.devEui);  // other field untouched
}

void test_invalid_euis_are_rejected() {
  TEST_ASSERT_TRUE(setField(g_cfg, Field::LoRaWanDevEui, "70B3D57ED0001234", g_err, sizeof(g_err)));
  TEST_ASSERT_FALSE(setField(g_cfg, Field::LoRaWanDevEui, "70B3D57ED000123", g_err, sizeof(g_err)));    // 15 digits
  TEST_ASSERT_FALSE(setField(g_cfg, Field::LoRaWanDevEui, "70B3D57ED00012345", g_err, sizeof(g_err)));  // 17
  TEST_ASSERT_FALSE(setField(g_cfg, Field::LoRaWanDevEui, "70B3D57ED000123G", g_err, sizeof(g_err)));
  TEST_ASSERT_FALSE(setField(g_cfg, Field::LoRaWanDevEui, "", g_err, sizeof(g_err)));
  TEST_ASSERT_NOT_NULL(strstr(g_err, "16 hex digits"));
  TEST_ASSERT_EQUAL_STRING("70B3D57ED0001234", g_cfg.uplinks[0].lorawan.devEui);
}

void test_eui_without_lorawan_uplink() {
  g_cfg.uplinkCount = 0;
  TEST_ASSERT_FALSE(setField(g_cfg, Field::LoRaWanDevEui, "70B3D57ED0001234", g_err, sizeof(g_err)));
  TEST_ASSERT_NOT_NULL(strstr(g_err, "no LoRaWAN uplink"));
}

void test_edited_config_survives_roundtrip() {
  setField(g_cfg, Field::Name, "Olpe-017", g_err, sizeof(g_err));
  setField(g_cfg, Field::LoRaWanDevEui, "70B3D57ED0001234", g_err, sizeof(g_err));
  char json[4096];
  const size_t len = encode(g_cfg, json, sizeof(json));
  TEST_ASSERT_TRUE(len > 0);
  static StationConfig back;
  const DecodeResult r = decode(json, len, back);
  TEST_ASSERT_TRUE_MESSAGE(r.ok, r.error);
  TEST_ASSERT_EQUAL_STRING("Olpe-017", back.name);
  TEST_ASSERT_EQUAL_STRING("70B3D57ED0001234", back.uplinks[0].lorawan.devEui);
  TEST_ASSERT_EQUAL(6, back.uplinks[0].lorawan.channelCount);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_field_paths);
  RUN_TEST(test_set_name);
  RUN_TEST(test_set_euis_normalises_input);
  RUN_TEST(test_invalid_euis_are_rejected);
  RUN_TEST(test_eui_without_lorawan_uplink);
  RUN_TEST(test_edited_config_survives_roundtrip);
  return UNITY_END();
}
