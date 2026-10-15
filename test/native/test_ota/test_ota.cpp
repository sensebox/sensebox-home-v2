#include <string.h>
#include <unity.h>

#include <string>

#include "ImageCheck.h"
#include "ImageMarker.h"
#include "SelfTest.h"

using ota::CheckError;
using ota::MarkerScanner;

namespace {

// A fake firmware image: ESP32-S2 header, padding, `marker`, padding.
std::string fakeImage(const std::string& marker, uint16_t chipId = 0x0002) {
  std::string img(24, '\0');
  img[0] = static_cast<char>(0xE9);
  img[12] = static_cast<char>(chipId & 0xFF);
  img[13] = static_cast<char>(chipId >> 8);
  img += std::string(5000, '\x55');
  img += marker;
  img += std::string(3000, '\xAA');
  return img;
}

void feedInChunks(MarkerScanner& s, const std::string& img, size_t chunk) {
  for (size_t i = 0; i < img.size(); i += chunk) {
    const size_t n = img.size() - i < chunk ? img.size() - i : chunk;
    s.feed(reinterpret_cast<const uint8_t*>(img.data() + i), n);
  }
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_header_check() {
  const std::string img = fakeImage("");
  const auto* p = reinterpret_cast<const uint8_t*>(img.data());
  TEST_ASSERT_EQUAL(static_cast<int>(CheckError::None), static_cast<int>(ota::checkHeader(p, img.size())));
  TEST_ASSERT_EQUAL(static_cast<int>(CheckError::NotAnApp), static_cast<int>(ota::checkHeader(p, 10)));
  const std::string esp32 = fakeImage("", 0x0000);
  TEST_ASSERT_EQUAL(static_cast<int>(CheckError::WrongChip),
                    static_cast<int>(ota::checkHeader(reinterpret_cast<const uint8_t*>(esp32.data()), esp32.size())));
  const uint8_t zip[16] = {'P', 'K', 3, 4};
  TEST_ASSERT_EQUAL(static_cast<int>(CheckError::NotAnApp), static_cast<int>(ota::checkHeader(zip, sizeof(zip))));
}

void test_marker_found_across_any_chunk_size() {
  const std::string img = fakeImage("@@SBH2-IMAGE@@ fw=0.2.0 hw=1.0,1.1;");
  for (size_t chunk : {1u, 3u, 7u, 13u, 1436u, 100000u}) {
    MarkerScanner s;
    feedInChunks(s, img, chunk);
    TEST_ASSERT_TRUE_MESSAGE(s.found(), "marker not found");
    TEST_ASSERT_EQUAL_STRING("0.2.0", s.version());
    TEST_ASSERT_EQUAL_STRING("1.0,1.1", s.hardware());
  }
}

void test_marker_after_lookalikes() {
  // The bare prefix (as a C string, followed by 0) and repeated '@' must not confuse the scanner.
  std::string noise = std::string("@@SBH2-IMAGE@@") + '\0' + "@@@SBH2-IMAGE@@@@SBH2-IMAGE@@ fw=0.3.0 hw=*;";
  MarkerScanner s;
  feedInChunks(s, fakeImage(noise), 5);
  TEST_ASSERT_TRUE(s.found());
  TEST_ASSERT_EQUAL_STRING("0.3.0", s.version());
  TEST_ASSERT_EQUAL_STRING("*", s.hardware());
}

void test_no_marker() {
  MarkerScanner s;
  feedInChunks(s, fakeImage("some other sketch"), 512);
  TEST_ASSERT_FALSE(s.found());
  // a broken marker (no hw field) is not accepted
  MarkerScanner s2;
  feedInChunks(s2, fakeImage("@@SBH2-IMAGE@@ fw=0.2.0;"), 512);
  TEST_ASSERT_FALSE(s2.found());
}

void test_own_marker_is_parseable() {
  // The marker compiled into this firmware is found by the scanner that runs in it.
  MarkerScanner s;
  const char* m = ota::imageMarker();
  s.feed(reinterpret_cast<const uint8_t*>(m), strlen(m));
  TEST_ASSERT_TRUE(s.found());
  TEST_ASSERT_EQUAL_STRING("*", s.hardware());
}

void test_hardware_compatibility() {
  TEST_ASSERT_TRUE(ota::hardwareCompatible("*", "1.2"));
  TEST_ASSERT_TRUE(ota::hardwareCompatible("1.0,1.2", "1.2"));
  TEST_ASSERT_FALSE(ota::hardwareCompatible("1.0,1.2", "1.1"));
  TEST_ASSERT_FALSE(ota::hardwareCompatible("1.0,1.21", "1.2"));  // no prefix matches
  TEST_ASSERT_TRUE(ota::hardwareCompatible("1.0", ""));           // unknown board revision
}

void test_self_test() {
  SelfTest t;
  t.begin(false);
  TEST_ASSERT_EQUAL(static_cast<int>(SelfTest::Result::NotNeeded), static_cast<int>(t.tick(999999, true)));

  t.begin(true);
  TEST_ASSERT_EQUAL(static_cast<int>(SelfTest::Result::Running), static_cast<int>(t.tick(60000, true)));
  TEST_ASSERT_EQUAL_UINT32(60, t.secondsLeft(60000));
  TEST_ASSERT_EQUAL(static_cast<int>(SelfTest::Result::Passed),
                    static_cast<int>(t.tick(SelfTest::kPassAfterMs, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(SelfTest::Result::NotNeeded), static_cast<int>(t.tick(SelfTest::kPassAfterMs + 1, true)));

  SelfTest bad;
  bad.begin(true);
  TEST_ASSERT_EQUAL(static_cast<int>(SelfTest::Result::Failed),
                    static_cast<int>(bad.tick(SelfTest::kPassAfterMs, false)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_header_check);
  RUN_TEST(test_marker_found_across_any_chunk_size);
  RUN_TEST(test_marker_after_lookalikes);
  RUN_TEST(test_no_marker);
  RUN_TEST(test_own_marker_is_parseable);
  RUN_TEST(test_hardware_compatibility);
  RUN_TEST(test_self_test);
  return UNITY_END();
}
