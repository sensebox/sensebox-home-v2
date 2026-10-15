#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ImageCheck.h"

// OTA updates on the device (plan §4.9): writes an uploaded image into the inactive app slot
// (app0/app1), and manages the bootloader's rollback state.
//
// Flow:
//   1. Service mode receives the image (web upload) and streams it through ImageUpload into the
//      other slot. Header, image marker (this firmware?) and hardware revision are checked while
//      it arrives; esp_ota_end() verifies the image checksum/hash.
//   2. On success the other slot becomes the boot slot and the box restarts into normal mode.
//   3. The new firmware starts in state "pending verify" and runs the self-test (SelfTest.h).
//      It marks itself valid, or rolls back. A reset before that rolls back in the bootloader.
//
// Not implemented yet (decision 2026-10-15): signatures, anti-downgrade.

namespace updater {

// --- boot / running firmware -------------------------------------------------------

struct RunningInfo {
  char partition[17];      // "app0" / "app1"
  bool pendingVerify;      // freshly updated, self-test not passed yet
  bool previousRolledBack; // the last update was rolled back (another slot is marked invalid)
  const char* marker;      // image marker of the running firmware (ImageMarker.h)
};

// Call once at boot (before the core would mark the image valid: see verifyRollbackLater()).
RunningInfo bootInfo();

// Keeps the running firmware (self-test passed).
bool markValid();
// Rolls back to the previous firmware and restarts. Does not return on success.
void rollback();

// --- upload --------------------------------------------------------------------------

class ImageUpload {
 public:
  // `boardRevision`: hardware revision of this board ("" if unknown).
  bool begin(const char* boardRevision);
  // Writes the next chunk. Returns false once an error occurred (the rest is ignored).
  bool write(const uint8_t* data, size_t len);
  // Finishes, verifies and activates the image for the next boot.
  bool end();
  void abort();

  bool failed() const { return error_[0] != '\0'; }
  const char* error() const { return error_; }
  const char* newVersion() const { return scanner_.version(); }
  size_t written() const { return written_; }

 private:
  void setError(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  uint32_t handle_ = 0;  // esp_ota_handle_t
  bool started_ = false;
  bool headerChecked_ = false;
  char boardRevision_[16] = {};
  ota::MarkerScanner scanner_;
  size_t written_ = 0;
  char error_[96] = {};
};

}  // namespace updater
