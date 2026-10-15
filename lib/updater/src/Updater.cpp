#include "Updater.h"

#include <esp_ota_ops.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ImageMarker.h"
#include "Log.h"

// Arduino core hook: by default the core marks a freshly updated image valid right at startup.
// Returning true defers that, so our self-test decides (main.cpp, SelfTest.h).
// extern "C": the core (esp32-hal-misc.c) is C. With C++ linkage this would silently not
// replace the core's weak default, and every update would count as good immediately.
extern "C" bool verifyRollbackLater() { return true; }

namespace updater {
namespace {
constexpr const char* kTag = "ota";
}

RunningInfo bootInfo() {
  RunningInfo info{};
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running != nullptr) {
    strncpy(info.partition, running->label, sizeof(info.partition) - 1);
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) info.pendingVerify = state == ESP_OTA_IMG_PENDING_VERIFY;
  }
  info.previousRolledBack = esp_ota_get_last_invalid_partition() != nullptr;
  // Also keeps the marker in the image: unreferenced, the linker drops it, and then this firmware
  // would refuse updates to itself.
  info.marker = ota::imageMarker();
  return info;
}

bool markValid() { return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK; }

void rollback() { esp_ota_mark_app_invalid_rollback_and_reboot(); }

void ImageUpload::setError(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(error_, sizeof(error_), fmt, args);
  va_end(args);
}

bool ImageUpload::begin(const char* boardRevision) {
  abort();
  error_[0] = '\0';
  written_ = 0;
  headerChecked_ = false;
  scanner_.reset();
  strncpy(boardRevision_, boardRevision != nullptr ? boardRevision : "", sizeof(boardRevision_) - 1);
  boardRevision_[sizeof(boardRevision_) - 1] = '\0';

  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr) {
    setError("no second firmware slot (was the box flashed with this firmware's partition table?)");
    return false;
  }
  esp_ota_handle_t h = 0;
  // Sequential writes: flash sectors are erased as the data arrives, not all up front (which
  // would block for seconds).
  const esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &h);
  if (err != ESP_OK) {
    setError("could not start the update (%s)", esp_err_to_name(err));
    return false;
  }
  handle_ = h;
  started_ = true;
  LOGI(kTag, "receiving firmware into %s", target->label);
  return true;
}

bool ImageUpload::write(const uint8_t* data, size_t len) {
  if (!started_ || failed()) return false;
  if (!headerChecked_) {
    const ota::CheckError e = ota::checkHeader(data, len);
    if (e != ota::CheckError::None) {
      setError("%s", ota::checkErrorText(e));
      abort();
      return false;
    }
    headerChecked_ = true;
  }
  const bool hadMarker = scanner_.found();
  scanner_.feed(data, len);
  if (!hadMarker && scanner_.found() && !ota::hardwareCompatible(scanner_.hardware(), boardRevision_)) {
    setError("%s (image: %s, this board: %s)", ota::checkErrorText(ota::CheckError::IncompatibleHardware),
             scanner_.hardware(), boardRevision_);
    abort();
    return false;
  }
  const esp_err_t err = esp_ota_write(handle_, data, len);
  if (err != ESP_OK) {
    setError("writing to flash failed (%s)%s", esp_err_to_name(err),
             err == ESP_ERR_INVALID_SIZE ? ": image larger than the firmware slot" : "");
    abort();
    return false;
  }
  written_ += len;
  return true;
}

bool ImageUpload::end() {
  if (!started_ || failed()) {
    if (!failed()) setError("no upload in progress");
    return false;
  }
  if (!scanner_.found()) {
    setError("%s", ota::checkErrorText(ota::CheckError::NotThisFirmware));
    abort();
    return false;
  }
  started_ = false;
  esp_err_t err = esp_ota_end(handle_);  // verifies checksum and appended SHA-256
  if (err != ESP_OK) {
    setError("image is damaged or incomplete (%s)", esp_err_to_name(err));
    return false;
  }
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  err = target != nullptr ? esp_ota_set_boot_partition(target) : ESP_ERR_NOT_FOUND;
  if (err != ESP_OK) {
    setError("could not activate the new firmware (%s)", esp_err_to_name(err));
    return false;
  }
  LOGI(kTag, "firmware %s written to %s (%u bytes), active after restart", scanner_.version(), target->label,
       static_cast<unsigned>(written_));
  return true;
}

void ImageUpload::abort() {
  if (started_) esp_ota_abort(handle_);
  started_ = false;
}

}  // namespace updater
