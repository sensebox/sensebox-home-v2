#pragma once

#include <stddef.h>
#include <stdint.h>

// Reset reason, breadcrumbs, heap figures and the task watchdog (plan §4.7).

namespace sysinfo {

// Call first thing in setup(): reads the reset reason and the breadcrumb left by the previous
// run (kept in RTC memory that survives software resets and watchdog resets).
void begin();

const char* resetReason();
// Module / state the previous run was in when it reset ("" if unknown, e.g. after power-on).
const char* previousBreadcrumb();
// True if the last reset was caused by a crash, watchdog or brown-out.
bool lastResetWasAbnormal();

// Records what the firmware is doing right now, for post-mortem analysis. Cheap; call at
// phase changes, not in tight loops. `where` is copied (truncated to 23 chars).
void breadcrumb(const char* where);

uint32_t freeHeap();
uint32_t minFreeHeap();
uint32_t largestFreeBlock();

// Task watchdog for the calling task (5 s, configured by the framework).
void watchdogAddCurrentTask();
void watchdogFeed();

uint64_t uptimeMs();

}  // namespace sysinfo
