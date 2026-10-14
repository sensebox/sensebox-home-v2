#include "BootMode.h"

#include <esp_attr.h>

namespace bootmode {
namespace {

constexpr uint32_t kServiceRequest = 0x5E7F1CE5;

// RTC slow memory: keeps its content across software and watchdog resets.
// After power-on it holds garbage, hence the magic value / checksum.
RTC_NOINIT_ATTR uint32_t g_request;
RTC_NOINIT_ATTR StatusSnapshot g_snapshot;

}  // namespace

StatusSnapshot& snapshot() { return g_snapshot; }

void requestServiceMode() { g_request = kServiceRequest; }

bool consumeServiceModeRequest() {
  const bool requested = g_request == kServiceRequest;
  g_request = 0;
  return requested;
}

}  // namespace bootmode
