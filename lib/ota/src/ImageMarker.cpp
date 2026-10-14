#include "ImageMarker.h"

#include "Version.h"

namespace ota {
namespace {

// Must match the format parsed by MarkerScanner: "<prefix> fw=<version> hw=<list>;"
const char kImageMarker[] __attribute__((used)) = "@@SBH2-IMAGE@@ fw=" SBH_FW_VERSION " hw=" SBH_HW_COMPAT ";";

}  // namespace

const char* imageMarker() { return kImageMarker; }

}  // namespace ota
