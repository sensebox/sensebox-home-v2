#pragma once

#include "StatusSnapshot.h"

// Normal mode vs. service mode (plan §4.8).
//
// Service mode is entered by a restart: the running firmware stores a status snapshot and a
// one-shot request in RTC memory (survives software restarts, not power loss), then restarts.
// The next boot consumes the request and starts service mode. Any later restart starts normal
// mode again, so "reboot" (or power-cycling) is how service mode is left.

namespace bootmode {

// The snapshot area in RTC memory. Valid only if snapshot::isValid() says so.
StatusSnapshot& snapshot();

// Marks the next boot as service-mode boot.
void requestServiceMode();

// Call once early in setup(): returns true if this boot was requested as service mode, and
// clears the request so the following boot is normal again.
bool consumeServiceModeRequest();

}  // namespace bootmode
