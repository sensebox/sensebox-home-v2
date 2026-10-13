#pragma once

// Firmware version. Release builds set SBH_FW_VERSION via build flags.
#ifndef SBH_FW_VERSION
#define SBH_FW_VERSION "0.1.0-dev"
#endif

// Hardware revisions this firmware is built for (checked again by OTA, plan §4.9).
#define SBH_COMPATIBLE_HW "mcu-s2"
