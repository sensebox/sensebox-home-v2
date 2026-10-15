#pragma once

#include <stddef.h>
#include <stdint.h>

// Checks for firmware images uploaded for an OTA update (plan §4.9). Pure logic, host-tested.
//
// Every image of this firmware contains an image marker (ImageMarker.cpp):
//     "@@SBH2-IMAGE@@ fw=<firmware version> hw=<compatible hardware revisions>;"
// The updater scans the uploaded bytes for it. An image without the marker is not a
// senseBox:home v2 firmware (e.g. a Blockly sketch or the stock APOTA image) and is refused.
// `hw` is "*" (any) or a comma-separated list such as "1.0,1.1".
//
// Not checked yet (decision 2026-10-15): signatures, anti-downgrade.

namespace ota {

enum class CheckError : uint8_t {
  None = 0,
  NotAnApp,              // first byte isn't the ESP image magic
  WrongChip,             // built for another ESP32 variant
  NotThisFirmware,       // no image marker found
  IncompatibleHardware,  // marker found, but this board's revision isn't listed
};

const char* checkErrorText(CheckError e);

// Checks the image header (first `len` bytes, at least 16). ESP32-S2 only.
CheckError checkHeader(const uint8_t* data, size_t len);

// True if `boardRevision` is in `hwList` ("*" = any). An unknown board revision (empty, e.g. no
// device identity provisioned) is accepted.
bool hardwareCompatible(const char* hwList, const char* boardRevision);

// Finds the image marker in a byte stream that arrives in chunks.
class MarkerScanner {
 public:
  static constexpr size_t kFieldLen = 32;

  MarkerScanner();
  void reset();
  void feed(const uint8_t* data, size_t len);

  bool found() const { return found_; }
  const char* version() const { return version_; }
  const char* hardware() const { return hardware_; }

 private:
  static constexpr size_t kPrefixMax = 16;
  static constexpr size_t kBodyMax = 80;

  void feedByte(uint8_t c);
  bool parseBody();

  size_t prefixLen_;
  uint8_t fail_[kPrefixMax];  // KMP failure table of the prefix
  size_t matched_ = 0;
  bool inBody_ = false;
  char body_[kBodyMax + 1] = {};
  size_t bodyLen_ = 0;
  bool found_ = false;
  char version_[kFieldLen] = {};
  char hardware_[kFieldLen] = {};
};

}  // namespace ota
