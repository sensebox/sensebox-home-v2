#pragma once

#include <stddef.h>
#include <stdint.h>

#include "StationConfig.h"
#include "StatusSnapshot.h"

// Service mode (plan §4.8): WiFi access point + captive portal + web UI / REST API.
//
// Runs INSTEAD of normal operation (no sensors, no LoRaWAN): the MCU S2 has no PSRAM and the
// WiFi stack alone needs a large part of the RAM. The status of normal operation comes from the
// snapshot taken before the restart. Leaving service mode = restart (reboot button in the web UI,
// `reboot` on the console, power cycle, the inactivity timeout, or no device connected for
// `idleExitSec`).
// Activity = a device joining the AP, page loads, user actions in the web UI and console input.
// A phone that just stays connected, the UI's automatic status poll and the OS's captive-portal
// probes do not count.
//
// REST API (all JSON):
//   GET  /api/status        service info + snapshot (without log)
//   GET  /api/log           log before service mode (from the snapshot)
//   GET  /api/config        stored config (no secrets), appKeySet, differsFromRunning
//   POST /api/config        complete config JSON
//   POST /api/config/field  {"field":"name"|"uplinks.devEui"|"uplinks.joinEui","value":"..."}
//   POST /api/appkey        {"appKey":"<32 hex>"}  (write-only)
//   POST /api/service-password {"password":"..."}  WiFi password of this AP (write-only)
//   POST /api/reboot        restart into normal mode
//   POST /api/ota           firmware update: multipart/form-data with the .bin file (any field
//                           name); on success the box restarts into the new firmware
//   GET  /api/sdlogs[?before=<name>]  log files on the SD card, newest first, 40 per page (name,
//                           size, mtime), card size/free, `more`; next page: before=<oldest shown>
//   GET  /api/sdlogs/file?name=B000042-0001.log   download one log file
//   GET  /api/sddata[?before=<name>]  measurement files (CSV) on the SD card, same as /api/sdlogs
//   GET  /api/sddata/file?name=B000042-0001.csv   download one measurement file
// The SD card is only mounted while one of the /api/sd* requests runs (RAM, see .cpp).

class ServiceMode {
 public:
  struct Options {
    const config::StationConfig* running = nullptr;
    const StatusSnapshot* snapshot = nullptr;  // nullptr if no valid snapshot
    const char* deviceSerial = "";             // for the SSID; MAC is used if empty
    const char* boardRevision = "";            // hardware revision, checked against OTA images
    uint16_t apTimeoutMin = 15;
    uint16_t idleExitSec = 60;  // 0 = off
  };

  // Starts AP, DNS and web server. Returns false if WiFi or memory fails.
  bool begin(const Options& options);
  // Call from the main loop. Restarts the box after the inactivity timeout or a reboot request.
  void loop(uint32_t nowMs);
  // Restarts the inactivity timeout (for activity outside the web server, e.g. the console).
  void markActivity();

  const char* ssid() const;
  uint8_t clients() const;
  // Seconds until the inactivity timeout restarts the box.
  uint32_t secondsLeft(uint32_t nowMs) const;
};

// Name of the service-mode access point: senseBox-<serial>, or the end of the MAC.
void serviceApSsid(char* out, size_t cap, const char* deviceSerial);
