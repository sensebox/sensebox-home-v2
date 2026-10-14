# senseBox:home v2 firmware

PlatformIO firmware for the senseBox:home v2 (senseBox MCU S2, ESP32-S2). It is one firmware for all stations: sensors, intervals, storage and data targets come from runtime configuration, not from build variants. The first deployment is ~50 outdoor stations for the city of Olpe: AM2315C + SPS30, LoRaWAN/TTN with Cayenne LPP → openSenseMap, SD card, mains power.

## Source of truth
- **Implementation plan:** `implementation-docs/IMPLEMENTATION_PLAN.md`. It holds the architecture, the decisions (§1.1), the phases and the open questions. Read the relevant sections before working on a phase. When a decision changes, update the plan; don't leave the change only in code or chat.
- Requirements (German), outside this repo: `../requirements/`. The pin map is `../requirements/pins_senseBox_MCUS2.h` (identical to the framework variant `sensebox_mcu_esp32s2/pins_arduino.h`, so the `PIN_*` names are available directly).
- Old v1 templates (outside this repo, reference only, don't copy their style): `../old/`. Design inspiration: `../senseBox-bike/`.
- All paths here are relative to the repo root (`new/`).

## Commands
PlatformIO isn't on PATH; use `~/.platformio/penv/bin/pio`.
- Build: `pio run -e mcu_s2` (release) or `pio run -e mcu_s2_dev` (debug logs, uses `config/olpe-template.json` when NVS has no config)
- Native unit tests: `pio test -e native` (all pure logic; must stay green)
- Flash + monitor: `pio run -e mcu_s2_dev -t upload && pio device monitor`
- Serial console on the device: type `help` (status, config, config-set, appkey, service-password, log, reboot)
- Service mode: hold BOOT ≥ 3 s and release (the box restarts into it); leave it with any restart. Web UI source is `web/index.html` (embedded at build time, no external resources allowed)

## Code layout
- `src/` composition root only (`main.cpp`) plus the serial console.
- `lib/<module>/src/`: `core` (data model, logger, clock), `config`, `health`, `sensors` (interface + manager),
  `pipeline` (quality, calibration, aggregation), `uplinks` (Cayenne, airtime, uplink logic): all host-testable.
  Also host-testable: `netutil` (captive DNS answers), the snapshot in `health`, `storage` (SD history/log files and janitor, behind the `FileSystem` interface).
  Device-only: `board`, `drivers`, `system` (NVS, reset info, watchdog, boot mode, config editing), `lorawan` (LMIC), `service` (AP, web server, REST API), `sdcard` (SD card `FileSystem`); these are in `lib_ignore` of the `native` env.
- `variants/sensebox_home_v2/`: own board variant (see gotchas).
- `test/native/test_*`: Unity tests, one folder per area.

## Architecture rules (non-negotiable)
- **Boot modes:** normal operation (incl. safe mode) and service mode never run in the same boot (`lib/system/src/BootMode.h`, `src/main.cpp`). Don't add WiFi/web features to normal operation or measurement/uplink work to service mode. Anything service mode should show about normal operation goes into the snapshot (`lib/health/src/StatusSnapshot.h`, filled in `src/StatusCapture.cpp`, kept in RTC memory; keep it below 4 KB, a test checks that).
- **Module boundaries:** modules live in `lib/<module>/`. Sensor code knows nothing about uplinks, storage or transport. Uplinks see only `Measurement`s, never concrete sensors. Concrete classes are wired together only in `src/main.cpp` and the driver registry.
- **A failure never takes down the box:** a failing sensor, SD card, radio or backend sets its status to `ERROR`, is retried with backoff, and is reported to Health. Everything else keeps running. No `while(1);` and no `ESP.restart()` as error handling (planned reboots go through the health supervisor).
- **No unbounded waits:** every wait has a timeout, and nothing like `while (!sensor.ready())`. Drivers are state machines driven by `tick()`. Documented exception: `Adafruit_AHTX0::getEvent()` blocks ~80 ms. Keep the comment where it's called.
- **Internal RAM is scarce, and not every MCU S2 has PSRAM.** ~9 KB static headroom with WiFi linked, and WiFi needs ~40 KB free heap at runtime (below that, WPA2 connections fail silently). Permanent buffers: small and static (e.g. the log ring is 40 lines). Buffers for console commands, config edits and web requests: per call with `sysinfo::makeTemp<T>()` / `makeTempBuffer(n)` (freed automatically). Check `RAM:` in the build output and the `internal heap free` value logged when service mode opens.
- **No heap churn on the measurement path:** use fixed-size structs, static ring buffers and `char[]`. No `String`, `std::vector`, `std::function` or `new`/`delete` after setup. ArduinoJson only for config import/export and the REST API; service-mode request handlers may use `String`/heap (the `WebServer` API needs it), the measurement path never.
- **LMIC is owned by its own task:** all `LMIC_*` / `os_*` calls happen in the LMIC task, and everything else communicates with it through FreeRTOS queues.
- **Time:** UTC internally (epoch seconds). Timezones only for display. Values recorded before the first time sync are flagged `Q_TIME_UNSYNCED`.
- **Raw values are never overwritten:** calibration produces a separate `Corrected` value, and aggregation a separate `Aggregated` value. Quality checks *flag* values, they don't drop them.
- **Config:** a versioned JSON document (`schemaVersion`) in NVS with migration steps. Never delete an old config; migrate it. Secrets (LoRaWAN AppKey, later WiFi PSK / OSeM token) live in a separate NVS namespace, are write-only over the API, and never appear in exports, logs, diagnostic reports or on the SD card.
- **Logging:** use the logger macros with module tags (`LOGE/LOGW/LOGI/LOGD("sps30", ...)`), never bare `Serial.print`.

When adding a feature, check it against these five questions:
1. Does it need a new firmware variant? (it shouldn't)
2. Does a module know a concrete sensor unnecessarily? (it shouldn't)
3. What happens if it fails? (the rest keeps running)
4. Does the setting survive an update? (it must)
5. Can the user see the error in the web UI? (they must)

## Hardware facts and gotchas
- **Sensor I2C** is QWIIC on SDA 39 / SCL 40, at 100 kHz (the SPS30 limit). AM2315C is at `0x38`, SPS30 at `0x69`.
- **LoRa-Bee in XB1:** CS 34, RESET 38, power 41. DIO0 and DIO1 are **both on INT 33** (same as the v1 LMIC pin map).
- **SD card** has its own SPI bus (HSPI: CS 10, MOSI 11, SCK 12, MISO 13) and a power switch on pin 9 (`SD_ENABLE`, LOW = on, as in the official MCU-S2 examples). Used in normal operation only; all SD access from the main loop.
- **Service mode is the BOOT button (GPIO0), pressed ≥ 3 s while running.** GPIO0 is a strapping pin: held during reset, it enters download mode. Input with pull-up only, never drive it.
- **`IO_ENABLE` (8) is not managed by the firmware;** the board variant switches it on at boot.
- **Own board variant** (`variants/sensebox_home_v2`): the stock `sensebox_mcu_esp32s2` variant blocks 5 s at every boot and switches to `ota_1` if BOOT is pressed then. That would break our OTA/rollback, so we use a copy without it. Don't switch back to the stock variant.
- **Adafruit AHTX0 busy-waits without a bound** in `getEvent()`/`begin()` (a failed status read looks like "busy" forever). The driver checks the sensor answers first; a hang there ends in a task-watchdog reset.
- **Bootloader:** app rollback is enabled in the framework's bootloader (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`), task WDT 5 s, core dump to flash.
- **Cayenne/openSenseMap:** the openSenseMap reads TTN's decoded payload (the TTN formatter must be set to CayenneLPP) and stamps values with the gateway receive time. So there is no LoRa backlog catch-up. A missing channel is stored as 0 on the openSenseMap (accepted for now). PM is sent as `analog_in`, clamped at 327.67.

## Code style
- C++17, no exceptions, no RTTI. clang-format (config in repo). `-Wall -Wextra` clean.
- Pure logic (data model, quality checks, aggregation, encoders, config migration, storage policies) must compile for the `native` env and gets unit tests. Hardware access goes through small interfaces (e.g. `II2CBus`) so drivers can be tested against fakes.
- Pin library versions exactly in `platformio.ini`.
- Match the surrounding code: comment density, naming, idioms.
