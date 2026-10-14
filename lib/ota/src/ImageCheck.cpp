#include "ImageCheck.h"

#include <string.h>

#include "ImageMarker.h"

namespace ota {
namespace {

constexpr uint8_t kImageMagic = 0xE9;      // ESP_IMAGE_HEADER_MAGIC
constexpr uint16_t kChipIdEsp32S2 = 0x0002;  // ESP_CHIP_ID_ESP32S2
constexpr size_t kChipIdOffset = 12;       // esp_image_header_t::chip_id (little endian)

bool copyField(const char* body, const char* key, char* out, size_t cap) {
  const char* p = strstr(body, key);
  if (p == nullptr) return false;
  p += strlen(key);
  size_t n = 0;
  while (p[n] != '\0' && p[n] != ' ' && p[n] != ';') ++n;
  if (n == 0 || n >= cap) return false;
  memcpy(out, p, n);
  out[n] = '\0';
  return true;
}

}  // namespace

const char* checkErrorText(CheckError e) {
  switch (e) {
    case CheckError::None: return "ok";
    case CheckError::NotAnApp: return "not an ESP32 firmware image (.bin)";
    case CheckError::WrongChip: return "firmware image for a different chip (not ESP32-S2)";
    case CheckError::NotThisFirmware: return "not a senseBox:home v2 firmware image";
    case CheckError::IncompatibleHardware: return "firmware is not made for this hardware revision";
  }
  return "unknown error";
}

CheckError checkHeader(const uint8_t* data, size_t len) {
  if (data == nullptr || len < kChipIdOffset + 2 || data[0] != kImageMagic) return CheckError::NotAnApp;
  const uint16_t chip = static_cast<uint16_t>(data[kChipIdOffset] | (data[kChipIdOffset + 1] << 8));
  return chip == kChipIdEsp32S2 ? CheckError::None : CheckError::WrongChip;
}

bool hardwareCompatible(const char* hwList, const char* boardRevision) {
  if (boardRevision == nullptr || boardRevision[0] == '\0') return true;  // unknown board
  if (hwList == nullptr) return false;
  if (strcmp(hwList, "*") == 0) return true;
  const size_t want = strlen(boardRevision);
  const char* p = hwList;
  while (*p != '\0') {
    const char* end = strchr(p, ',');
    const size_t n = end != nullptr ? static_cast<size_t>(end - p) : strlen(p);
    if (n == want && strncmp(p, boardRevision, n) == 0) return true;
    if (end == nullptr) break;
    p = end + 1;
  }
  return false;
}

MarkerScanner::MarkerScanner() {
  prefixLen_ = strlen(kMarkerPrefix);
  if (prefixLen_ > kPrefixMax) prefixLen_ = kPrefixMax;
  // KMP failure table, so a match can continue across chunk boundaries and repeated '@'.
  fail_[0] = 0;
  size_t k = 0;
  for (size_t i = 1; i < prefixLen_; ++i) {
    while (k > 0 && kMarkerPrefix[i] != kMarkerPrefix[k]) k = fail_[k - 1];
    if (kMarkerPrefix[i] == kMarkerPrefix[k]) ++k;
    fail_[i] = static_cast<uint8_t>(k);
  }
  reset();
}

void MarkerScanner::reset() {
  matched_ = 0;
  inBody_ = false;
  bodyLen_ = 0;
  found_ = false;
  version_[0] = '\0';
  hardware_[0] = '\0';
}

void MarkerScanner::feed(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len && !found_; ++i) feedByte(data[i]);
}

void MarkerScanner::feedByte(uint8_t c) {
  if (inBody_) {
    // The body ends with ';'. A 0 byte or overlong body means this was another occurrence of the
    // prefix (e.g. the prefix constant used by the scanner itself): keep searching.
    if (c == ';') {
      body_[bodyLen_] = '\0';
      inBody_ = false;
      found_ = parseBody();
    } else if (c == 0 || bodyLen_ >= kBodyMax) {
      inBody_ = false;
    } else {
      body_[bodyLen_++] = static_cast<char>(c);
    }
    return;
  }
  while (matched_ > 0 && c != static_cast<uint8_t>(kMarkerPrefix[matched_])) matched_ = fail_[matched_ - 1];
  if (c == static_cast<uint8_t>(kMarkerPrefix[matched_])) ++matched_;
  if (matched_ == prefixLen_) {
    matched_ = 0;
    inBody_ = true;
    bodyLen_ = 0;
  }
}

bool MarkerScanner::parseBody() {
  return copyField(body_, "fw=", version_, sizeof(version_)) && copyField(body_, "hw=", hardware_, sizeof(hardware_));
}

}  // namespace ota
