#include "SystemInfo.h"

#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <string.h>

namespace sysinfo {
namespace {

constexpr uint32_t kMagic = 0x5B4E2026;
constexpr size_t kCrumbLen = 24;

struct Breadcrumb {
  uint32_t magic;
  char where[kCrumbLen];
};

// Survives software and watchdog resets, not power loss.
RTC_NOINIT_ATTR Breadcrumb g_crumb;

char g_previous[kCrumbLen] = {};
esp_reset_reason_t g_reason = ESP_RST_UNKNOWN;

}  // namespace

void begin() {
  g_reason = esp_reset_reason();
  if (g_crumb.magic == kMagic && g_reason != ESP_RST_POWERON) {
    memcpy(g_previous, g_crumb.where, kCrumbLen);
    g_previous[kCrumbLen - 1] = '\0';
  }
  g_crumb.magic = kMagic;
  breadcrumb("boot");
}

const char* resetReason() {
  switch (g_reason) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "other_watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

const char* previousBreadcrumb() { return g_previous; }

bool lastResetWasAbnormal() {
  switch (g_reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT: return true;
    default: return false;
  }
}

void breadcrumb(const char* where) {
  strncpy(g_crumb.where, where, kCrumbLen - 1);
  g_crumb.where[kCrumbLen - 1] = '\0';
}

uint32_t freeHeap() { return esp_get_free_heap_size(); }
uint32_t minFreeHeap() { return esp_get_minimum_free_heap_size(); }
uint32_t largestFreeBlock() { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }

void watchdogAddCurrentTask() { esp_task_wdt_add(nullptr); }
void watchdogFeed() { esp_task_wdt_reset(); }

uint64_t uptimeMs() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

}  // namespace sysinfo
