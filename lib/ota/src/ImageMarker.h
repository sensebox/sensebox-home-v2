#pragma once

// The image marker of the running firmware (see ImageCheck.h), e.g.
// "@@SBH2-IMAGE@@ fw=0.1.0-dev hw=*;". The compatible hardware revisions come from the build
// flag SBH_HW_COMPAT (default "*" = any board).

#ifndef SBH_HW_COMPAT
#define SBH_HW_COMPAT "*"
#endif

namespace ota {

constexpr const char* kMarkerPrefix = "@@SBH2-IMAGE@@";

// The marker of this firmware. It must be referenced at runtime (updater::bootInfo() does),
// otherwise the linker drops it - and the firmware would refuse updates to itself.
const char* imageMarker();

}  // namespace ota
