# senseBox:home v2 Firmware – Implementation Plan

Status: draft for discussion · updated 2026-10-13 (Olpe decisions in §1.1, Cayenne/OSeM findings in §6)

Inputs:
- [requirements/requirements.md](requirements/requirements.md): general requirements
- [requirements/olpe-specific.md](requirements/olpe-specific.md): the five guiding rules and the Olpe scope
- [requirements/issues-and-ideas.md](requirements/issues-and-ideas.md): problems seen with home v1 and ideas for v2
- [old/](old/): v1 sketch templates (WiFi / Ethernet / LoRa, generated per box)
- [senseBox-bike/](senseBox-bike/): design inspiration

---

## 1. Goals

1. **One firmware, many stations.** A single build runs on every senseBox:home v2 (MCU S2). The sensors, intervals, storage and data targets are set by **configuration**. Build-time templates (the `@@SENSOR_IDS@@` approach in `old/`) go away.
2. **First customer: Olpe.** About 50 outdoor stations with temperature/humidity (Adafruit AM2315C, an encased AHT20 probe) and fine particulates (SPS30). They upload over **LoRaWAN (TTN) with Cayenne LPP** to the openSenseMap through the TTN integration, with an SD card (fixed part of the hardware) and mains power.
3. **Easy to extend afterwards.** These should be additions, not rewrites:
   - new sensors (SCD30, RG-15, SMT50, …)
   - new paths: WiFi → openSenseMap HTTP, Ethernet, custom HTTP/MQTT targets
   - a station without data target that only records to its SD card

The plan sorts the work into an **Olpe MVP** (everything in phases 0–7) and **follow-up work** (phase 8+). The architecture is designed for the whole picture from the start.

### 1.1 Decisions (2026-10-13)

| Topic | Decision | Effect on the plan |
|---|---|---|
| LoRaWAN network | **TTN** | OSeM TTN integration with the `cayenne-lpp` profile can be used directly |
| SD card | **fixed part of the hardware**, always present (decision 2026-10-15; before: optional) | SD is the primary store; a missing or failing card is a hardware fault (health ERROR), measuring and sending continue; no `storage.sd` switch |
| Temp/humidity sensor | **AM2315C** ([Adafruit 5182](https://www.adafruit.com/product/5182), AHT20 inside), replacing the SHT45 | AHT20 driver in the Olpe release; SHT4x and HDC1080 drivers move to phase 8 (§6, "AM2315C") |
| Power | **mains** | no deep sleep, continuous SPS30 operation, the system clock runs continuously between syncs |
| Intervals | **measure every 60 s, upload every 10 min** | defaults in the config |
| LoRaWAN keys | **provided by reedu** in production | written by `tools/provision.py`, stored as secrets separate from the config (§5.1) |
| eFuses | **none**: no Secure Boot, no flash encryption | OTA signatures checked by the application only (§4.9) |
| External RTC | **none** | time from LoRaWAN `DeviceTimeReq` (§6.1) |
| Service-mode trigger | **BOOT button (GPIO0)** | long press during operation (§4.8) |
| Profiles | **none**; a single behaviour | config is authoritative, auto-detection only helps (§4.3) |
| Time sync interval | **every 72 h** (plus after every join) | §6.1 |
| PM values to OSeM | **all four** (PM1, PM2.5, PM4, PM10) | 23 B Cayenne frame (§6) |
| SD card size | **at least 4 GB** (decision 2026-10-15) | room for years of Olpe data; overflow protection still required (§4.5.1); a smaller card is used but flagged |
| Pin map | [requirements/pins_senseBox_MCUS2.h](requirements/pins_senseBox_MCUS2.h) | board layer (§4.7.1) |
| `IO_ENABLE` | **not used for now** | sensor recovery is I2C bus recovery only |
| PM > 327.67 µg/m³ | **clamp** (`analog_in`) | flagged; full value on SD |
| OSeM sensor titles | as in §6 finding 2 | shared `decodeOptions` for all boxes |
| Missing channel → 0 on OSeM | **accepted for now** | known limitation; integration fix moves to phase 8 |
| LoRaWAN library | **MCCI LMIC** | rejoin after every boot, dedicated LMIC task (§6.0) |

### 1.2 Implementation status (2026-10-13)

Done (builds for the MCU S2; 46 native unit tests pass; **not yet run on hardware**):
- Phase 0 (except hardware checks): PlatformIO project, partition table, own board variant, native test env.
- Phase 1: data model, logger with RAM ring, clock (UTC + unsynced timestamps), config (structs, JSON, validation, migration framework, NVS), health registry, boot guard / safe mode, reset reason + breadcrumb, task watchdog, status LED, BOOT button detection.
- Phase 2: sensor interface, SensorManager (interval, timeouts, backoff, health), AM2315C (Adafruit AHTX0) and SPS30 drivers, quality checks, calibration, aggregation.
- Phase 4 (most of it): Cayenne encoder, airtime + fair-use budget, Cayenne uplink (10-min means, no catch-up), LMIC transport in its own task (join, uplink, DeviceTimeReq → clock, radio reset).
- Serial console for dev/provisioning (status, config, AppKey).
- HDC1080 driver (Adafruit HDC1000 library) for bench testing and existing boxes.

- Phase 5, service mode (2026-10-14; not yet run on hardware): separate boot mode entered with a 3 s BOOT press, status snapshot in RTC memory, WPA2 access point with captive DNS, embedded web UI (status before service mode, settings incl. write-only AppKey, config JSON, logs, restart), REST API `/api/...`, inactivity timeout, `service-password` console command. Not done: auto-start when unconfigured or in safe mode, sensor check, diagnostic report download, browser time → clock. WebSocket live values are dropped: nothing is measured in service mode.

- Phase 3, SD card (2026-10-14; not yet run on hardware): measurement history (CSV) and log files per boot, buffered writes, unmount/power-cycle/retry on errors, hot-plug, janitor with watermarks, log cap and retention (`lib/storage`, `lib/sdcard`, §4.5). Not done: RAM record store and uplink outbox (only needed for a catch-up uplink, i.e. HTTP in phase 8), file download in service mode.

- Phase 6, OTA (2026-10-15; not yet run on hardware): upload in service mode (web UI, `POST /api/ota`), streamed into the other app slot; checks: ESP32-S2 image, image marker (this firmware), hardware revision, image hash; self-test (2 min normal operation + config accepted) with automatic rollback (`lib/ota`, `lib/updater`, §4.9). Not done (decision 2026-10-15): signatures, anti-downgrade.

- Main loop at 10 Hz (100 ms, fixed rate) instead of ~100 Hz (decision 2026-10-15). Time-critical LoRaWAN timing stays in the LMIC task; the serial console reads all waiting input per pass; in service mode each HTTP request may wait up to 100 ms before it is handled.

Not done yet: phase 7, `tools/` scripts, on-device tests.

Findings during implementation:
- The stock `sensebox_mcu_esp32s2` board variant blocks 5 s at boot and switches the boot partition to `ota_1` if BOOT is pressed. With our partition table that would bypass OTA rollback, so the firmware uses its own variant (`variants/sensebox_home_v2`) without it.
- The framework bootloader has app rollback enabled → phase 0 question answered. Flash 4 MB. **PSRAM is not guaranteed:** the board definition enables it, but the test board has none in the heap (confirmed on hardware 2026-10-13; the stock variant also distinguishes MCU S2 revisions with and without PSRAM). The firmware must run without it.
- Adafruit AHTX0's `getEvent()` / `begin()` busy-wait without an upper bound. Mitigated by a presence check before each measurement; a sensor vanishing mid-measurement still ends in a watchdog reset.
- SF12 airtime for the 23 B frame is ~2.0 s (the first draft said ~1.5 s).
- **Web server:** the framework's synchronous `WebServer` is used instead of ESPAsyncWebServer. It is polled from the main loop, so request handlers can use config/health/clock without locks, and it needs no extra libraries. WebSocket live values (phase 8) will need a different server or a small WebSocket add-on.
- **Internal RAM is the tightest resource.** On the ESP32-S2 static data shares 168 KB with code that must run from internal RAM; linking WiFi takes ~87 KB of it, and WiFi needs another ~40 KB of free heap at runtime (with only 10 KB free, WPA2 handshakes failed silently while open networks worked). Rules since then: permanent buffers stay small (log ring 16 lines since 2026-10-15; the full log is on the SD card); working buffers for console commands, config edits and web requests are allocated per call and freed (`sysinfo::makeTemp` / `makeTempBuffer`). Service mode warns in the log below 30 KB free. Measured on a board without PSRAM (2026-10-14): 32 KB free with the AP up, 15 KB after one notebook connected, then a crash on the first HTTP requests. Fixes: no mDNS, captive DNS polled in the main loop instead of the AsyncUDP task, max. 2 clients, smaller serial RX buffer, config decode scratch on the heap only while decoding. Static data headroom after phase 5: ~9.3 KB (code + static data use 162 of 168 KB). Phases 3 and 6 must budget their RAM explicitly.
- The firmware's partition table replaces the stock senseBox TinyUF2 layout: no **TinyUF2** (USB drag-and-drop flashing) and no **APOTA** (open-AP fallback updater in `ota_1`). APOTA accepts any image without checks and would bypass rollback; TinyUF2 costs 256 KB and brings its own bootloader. Flashing is via esptool/PlatformIO; updates in the field come with our own OTA (phase 6). If a DIY/education variant needs drag-and-drop flashing later, TinyUF2 can be added back as a partition (smaller app slots; its bootloader must then be checked for rollback support). Restoring a board to the stock layout needs esptool (files in the framework's `variants/sensebox_mcu_esp32s2/`).

---

## 2. Lessons from the existing code

### Things the senseBox:bike firmware does well (keep)
- There is one folder per module (`sensors/`, `ble/`, `display/`, `led/`).
- A common `BaseSensor` base class has a virtual `initSensor()` / `readSensorData()`.
- Sensors deliver values through a callback, so the consumer is decoupled from the producer.

### Things to change
| Bike / home v1 | v2 |
|---|---|
| The sensor class also writes to BLE (`notifyBLE`, `sendBLE`): sensors know about the transport | Sensors only produce `Measurement`s. Routing to storage and uplinks happens elsewhere (**DataRouter**). |
| `while (sps30_probe() != 0)` and `while(1);` in the HDC init: one missing sensor freezes the box | Non-blocking driver state machines with timeouts. A sensor in `ERROR` is retried with backoff. |
| `std::vector<float>`, `std::function`, `String` on every measurement | Fixed-size structs and static ring buffers on the hot path. Heap is used only at boot/config time. |
| The sensor list is hard-coded in `main.cpp` (`BaseSensor *sensors[]`) | A **driver registry** plus config: `main.cpp` builds the station from the config. |
| `delay()` everywhere, `Serial.println` debugging | A cooperative scheduler, a structured logger with levels and module tags, and a log ring buffer |
| `#ifdef XYZ_CONNECTED` and template placeholders (home v1) | Runtime config in NVS, with JSON import/export |
| Values are unlabelled float lists (`{pm1, pm2_5, pm4, pm10}`) | Every value carries its phenomenon, unit, timestamp, sensor ID and quality flags |
| Home v1 LoRa uses a custom `LoraMessage` byte layout | Cayenne LPP (standard, self-describing, supported by the OSeM TTN integration) |

---

## 3. Platform and toolchain decisions

| Topic | Decision | Rationale |
|---|---|---|
| Build system | **PlatformIO** | Requested; supports native unit tests and multiple envs |
| Framework | **Arduino-ESP32 3.x (ESP-IDF 5.x underneath)** via the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform, pinned to a fixed release | The official `espressif32` platform is stuck on Arduino 2.0.x (the bike pins 2.0.17). 3.x brings a current IDF: OTA rollback APIs, the newer task watchdog API, mbedTLS 3, and LittleFS fixes. Arduino keeps the sensor libraries usable. ESP-IDF APIs (NVS, OTA, WDT, `esp_reset_reason`) are called directly where needed. |
| Board | `sensebox_mcu_esp32s2` | 4 MB flash (verified). PSRAM depends on the board revision; the firmware must not rely on it. |
| Language | C++17, no exceptions, no RTTI in our code | Small, predictable binaries |
| JSON | ArduinoJson 7 | Used **only** for config import/export and the REST API, never in the measurement path |
| LoRaWAN stack | **MCCI LoRaWAN LMIC** (`mcci-catena/arduino-lmic`, pinned version), SX1276/RFM95 on XBee socket 1 | Decision 2026-10-13. Proven with the LoRa-Bee and the shared interrupt pin in home v1, and familiar to the team. It supports `DeviceTimeReq`. Its limits and how we handle them are in §6.0. The `IUplink` interface keeps the library swappable (RadioLib is an option for later, see phase 8). |
| Web server | ESPAsyncWebServer (maintained fork) + AsyncTCP | Non-blocking, with WebSocket support for the later live view |
| SPS30 library | Sensirion `arduino-i2c-sps30` (official, current) | Replaces the old embedded `sensirion-sps` C library used by the bike |
| Code style | clang-format, `-Wall -Wextra -Werror` on our sources | |

### Partition table (proposal for 4 MB flash; adjust after verification)
```
nvs        data nvs     20 KB    config, secrets, uplink cursors
otadata    data ota      8 KB
factory_id data nvs      8 KB    device identity (written once in production)
app0       app  ota_0  1.75 MB
app1       app  ota_1  1.75 MB
coredump   data coredump 64 KB   crash dumps for the diagnostic report
spiffs     data littlefs  rest   (optional) web UI assets if not embedded
```
The web UI is **embedded in the firmware image** (gzipped, generated at build time). Firmware and UI are then always the same version and are updated together by OTA. If the image grows too big, it can move to LittleFS later.

---

## 4. Architecture

### 4.1 Layer overview

```
                       ┌───────────────────────────────────────────────┐
                       │                 main.cpp                      │
                       │  composition root: reads config, builds the   │
                       │  station, starts the scheduler                │
                       └───────────────────────────────────────────────┘
 ┌──────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────────────┐
 │ Sensors      │──▶│ Pipeline     │──▶│ Storage      │──▶│ Uplinks (data targets)│
 │ drivers +    │   │ quality →    │   │ SD / RAM     │   │ LoRaWAN-Cayenne, OSeM │
 │ SensorManager│   │ aggregate →  │   │ outbox       │   │ HTTP, none, …         │
 └──────────────┘   │ DataRouter   │   └──────────────┘   └──────────┬───────────┘
                    └──────────────┘                                 │ needs
                                                          ┌──────────▼───────────┐
                                                          │ Connectivity          │
                                                          │ WiFi STA/AP, LoRa     │
                                                          │ radio, (Ethernet)     │
                                                          └──────────────────────┘
 Cross-cutting: Config · Logger · Clock (UTC) · Health/Diagnostics · Watchdog · Board/I2C
 Service:       ServiceMode (AP + captive portal) · REST API · Web UI · OTA · StatusLed/Display
```

**Dependency rule:** arrows point one way only. Sensor code does not know any uplink. Uplinks don't know any concrete sensor; they only see `Measurement`s. Concrete classes meet only in `main.cpp` (and the factory/registry).

### 4.2 Data model (fixed size, no heap)

```cpp
enum class Phenomenon : uint8_t { AirTemperature, RelativeHumidity, PM1, PM2_5, PM4, PM10,
                                  /* later: Pressure, Illuminance, UV, CO2, SoilMoisture, … */ };
enum class Unit : uint8_t { DegC, Percent, UgPerM3, HPa, Lux, Ppm, /* … */ };
enum class ValueKind : uint8_t { Raw, Corrected, Aggregated };

enum QualityFlag : uint16_t {           // bitmask, 0 = good
  Q_OUT_OF_RANGE   = 1 << 0,            // outside the sensor's physical range
  Q_IMPLAUSIBLE    = 1 << 1,            // outside the plausible environmental range
  Q_SPIKE          = 1 << 2,            // jump bigger than allowed vs. previous value
  Q_STUCK          = 1 << 3,            // identical value for N readings
  Q_WARMUP         = 1 << 4,            // sensor still in warm-up (e.g. SPS30 first 30 s)
  Q_COMM_RETRY     = 1 << 5,            // value obtained after a bus retry
  Q_TIME_UNSYNCED  = 1 << 6,            // timestamp derived from uptime, clock not yet synced
  Q_INCOMPLETE_AGG = 1 << 7,            // aggregate built from fewer samples than expected
};

struct Measurement {                    // ~20 bytes
  uint32_t   timestamp;                 // UTC epoch seconds (see Clock)
  float      value;
  uint8_t    sensorSlot;                // index of the configured sensor instance
  Phenomenon phenomenon;
  Unit       unit;
  ValueKind  kind;
  uint16_t   quality;                   // QualityFlag bitmask
};
```
- **Raw, corrected and aggregated** values are separate `ValueKind`s. A calibration correction (offset/gain from config) creates a *new* `Corrected` value; the raw value stays untouched. Uplinks are configured to send a chosen kind (Olpe: `Aggregated`). SD stores raw values plus aggregates.
- The **sensor ID** for the openSenseMap does not live in `Measurement`. It is resolved at the uplink from `(sensorSlot, phenomenon)` through the config mapping. That keeps the struct small and the target-specific IDs out of the core.

### 4.3 Sensor driver interface

Each driver is a non-blocking state machine. The `SensorManager` calls `tick()` frequently; drivers never sleep or busy-wait.

```cpp
struct SensorDescriptor {               // static, constexpr per driver → the "sensor metadata registry"
  const char*            type;          // "am2315c", "sps30", later "sht4x", "hdc1080"
  const char*            manufacturer;
  const PhenomenonSpec*  phenomena;     // phenomenon, unit, physical min/max, accuracy, resolution
  uint8_t                phenomenonCount;
  uint8_t                i2cAddresses[2];  // for detection
  uint32_t               warmupMs;
  uint32_t               minIntervalMs;
  bool                   outdoorSuitable;
  const char*            notes;         // mounting hints, e.g. "needs radiation shield"
};

enum class SensorState : uint8_t { Uninitialized, Initializing, WarmingUp, Ready, Measuring, Error, Disabled };

class ISensor {
public:
  virtual const SensorDescriptor& descriptor() const = 0;
  virtual void begin(const SensorConfig& cfg) = 0;     // starts init; returns immediately
  virtual void tick(uint32_t nowMs) = 0;               // advances state machine, must return in < ~5 ms
  virtual bool requestMeasurement() = 0;               // false if busy/not ready
  virtual uint8_t takeResults(Measurement* out, uint8_t max) = 0; // drains ready results
  virtual SensorState state() const = 0;
  virtual SensorError lastError() const = 0;
  virtual void reset() = 0;                            // power-cycle/re-init path
  virtual ~ISensor() = default;
};
```

- **Driver registry:** a static table of `{ "sps30", &descriptor, factory }` entries. Adding a sensor means adding a driver folder and one registry line. Nothing else changes (rule 2 in the Olpe document).
- **SensorManager** (one per box) handles:
  - scheduling per sensor interval
  - timeouts: a measurement that hasn't finished after `X` ms moves to `Error`
  - error counters, last error, last good measurement
  - retries with exponential backoff, e.g. 10 s → 20 s → … → max 10 min, including I2C bus recovery (clock out 9 SCL pulses)
  - publishing the status to Health
- **Auto-detection:** at boot, scan I2C and match the result against the registry. The configured sensor list is always authoritative:
  - a configured sensor that is missing → `ERROR` plus a diagnostic entry (never silently removed)
  - a detected sensor that isn't configured → shown in the web UI as "detected, not configured – add?", but never activated automatically

  This one behaviour fits both city deployments (fixed setup) and DIY kits (the user clicks "add"), so there are no "profiles".

**About "sensor metadata in a registry" (question in requirements.md):** we split it into three parts:
1. **Model metadata:** measured phenomena, unit, range, accuracy, warm-up, indoor/outdoor suitability, mounting hints. This is the same for every unit of a model, so it is compiled into the firmware (`SensorDescriptor`). The REST API exposes it and it goes into the diagnostic report. It is never sent over LoRa.
2. **Instance metadata:** serial number (read from the sensor where possible, e.g. SPS30; for sensors without a readable serial such as the AM2315C, entered at provisioning), calibration offsets, slot. This is part of the config/NVS and is shown in the diagnostic report.
3. **Deployment metadata:** location, mounting height, shield type, orientation… This is part of the config as a free JSON block. Later it is synced with the openSenseMap / the openSenseLab metadata model (SOSA/SSN), once that model is agreed.

### 4.4 Pipeline: quality, aggregation, routing
1. **QualityChecker** (pure logic, unit-tested natively) runs these checks against `SensorDescriptor` limits and configurable plausibility limits:
   - range check
   - plausibility check
   - spike check (max delta per time)
   - stuck check (N identical values)
   - warm-up flag

   Values are *flagged*, never silently dropped (requirement: don't change raw values unnecessarily).
2. **Calibration** (optional, per sensor/phenomenon from config): `corrected = raw * gain + offset`. It produces a second value of kind `Corrected`.
3. **Aggregator:** per phenomenon over the uplink interval it computes mean, plus min/max/count (kept internally, stored on SD). It ignores flagged samples, depending on policy.
4. **DataRouter:** pushes each value to the `Outbox` (storage) and notifies live consumers (web UI, display).

### 4.5 Storage and store-and-forward
```cpp
class IRecordStore {                    // append-only log of Measurement records
  virtual bool append(const Measurement*, uint8_t n) = 0;
  virtual uint32_t head() const = 0;    // monotonic sequence number
  virtual uint8_t read(uint32_t fromSeq, Measurement* out, uint8_t max) = 0;
};
```
- **Implemented (2026-10-14), differing from the first design below:** `SdStore` (`lib/storage`) writes **CSV text**, not binary records, and names files by **boot number and part** instead of by date (`/data/B000042-0001.csv`, `/logs/B000042-0001.log`; new part after 4 MB / 1 MB):
  - **Why not by date:** without RTC the date is unknown until the first LoRaWAN time sync, but recording must start at boot. Lines before the sync carry `uptime_s`, and a `# time synced` marker line allows converting them later.
  - **Why CSV:** readable without tools, and still small enough (~0.6 MB/day for Olpe).
  - **Writing:** buffered in RAM (2 KB measurements, 2 × 1 KB log, double-buffered so logging never waits for the card) and flushed every 15 s.
  - **Errors:** a write error unmounts the card, powers it off (`SD_ENABLE`) and retries with backoff; hot-plug is covered by the same retry.
  - **Separation:** the card is used in normal operation only. The file-system access is behind `FileSystem` (`lib/storage/src/FileSystem.h`; device implementation `lib/sdcard`, POSIX/VFS under `/sd`, own SPI bus HSPI), so the whole logic is host-tested with an in-memory card.
- *First design (not implemented):* **SdRecordStore**: one binary append-only file per day (`/data/2026/10/2026-10-09.bin`, one folder per year and month so no single FAT directory gets huge) plus a human-readable CSV export path for the web UI. It writes with a fixed record size, so random access by sequence number works without parsing.
- *Not implemented:* **RamRecordStore**: without PSRAM only a few hundred records fit; the SD write buffer bridges short outages, and LoRaWAN doesn't catch up anyway (§6.2).
- **Outbox:** each uplink keeps its own **cursor** (last acknowledged sequence number), persisted in NVS (written rarely, e.g. every N records, to protect flash). After a reconnect the uplink catches up from its cursor. This gives store-and-forward per target, and several targets can run independently at different speeds.
- **SD full:** see §4.5.1.
- **The card is a fixed part of the hardware (decision 2026-10-15), at least 4 GB.** Not finding it, or a card that fails, is a hardware fault: health `ERROR` (LED red). Measuring and sending continue; measurements are buffered in RAM from boot on (also before the first mount) and the card is retried with backoff, so a reseated card is picked up again. A card smaller than 4 GB is used but flagged in `status` and the log. There is no config switch for the card: `storage.sd` was removed (changed in place, no migration: no stations in the field yet, decision 2026-10-15).
- No secrets ever go on the SD card (decision by Jan).

#### 4.5.1 SD overflow protection (deleting old records)

**Expected volume for Olpe:**
- 6 values/min × ~20 B per record ≈ 175 KB/day ≈ **65 MB/year** of raw data, plus aggregates and logs
- a 4 GB card would last decades, but a misbehaving logger, a future 1 s-interval sensor or a smaller replacement card must not fill it up and stop recording

**What the SD card holds (as implemented):**

| Area | Path | Own cap |
|---|---|---|
| Measurements | `/data/B<boot>-<part>.csv` | none; it may use the space the logs leave |
| Logs | `/logs/B<boot>-<part>.log` | 10 % of the card, max. 100 MB (counted in 1 MB files; oldest deleted first) |

**Implemented janitor (`lib/storage/src/Janitor.h`):**
- **Watermarks:** below `lowWatermarkPct` free (default 10 %) it deletes until `highWatermarkPct` (20 %) is free: logs first, then measurement files, oldest first by name.
- **90 % is a hard limit (decision 2026-10-14):** the card may be filled up to 90 % (`lowWatermarkPct` 10); above that the oldest file is deleted whatever its age.
- **Retention:** measurement files younger than `minRetentionDays` (by FAT timestamp, known only for files written after a time sync) are only protected from the further cleanup between 90 % and 80 %.
- **Never deleted:** current files and files not matching the naming scheme.
- **Over the limit:** if nothing is deletable above 90 %, health shows `card over 90 % full`.
- **Schedule:** runs at mount and every 10 min; while cleaning, one file every 200 ms.
- **Cheap scan:** the directory scan reads names only (one `readdir` pass, no `stat` per file), so it stays fast with ~1000 files.

The original design follows for reference.

**Watermarks on the whole card:**
- **Low watermark:** free space < 10 % of the card or < 200 MB, whichever is larger. When reached, the `SdJanitor` deletes the **oldest whole day files**: logs first, then measurement files. It stops at the high watermark.
- **High watermark:** free space ≥ 20 %.
- **Order within measurements:** files that every uplink has already sent are deleted first. For LoRa this is always the case, because it doesn't catch up (§6.2). So "acknowledged" only matters for the future HTTP uplink.
- **Never deleted:** today's file, and any file younger than a configurable minimum age (default 30 days). If even that isn't enough space, recording continues in the RAM ring and the status becomes `ERROR: SD full`. *(First design; see "Implemented janitor" above.)*

**When it runs:**
- the janitor runs at boot, once per hour, and immediately on any write error that looks like "disk full" (`ENOSPC`)
- deleting happens in small steps (one file per scheduler tick), so the measurement loop is never blocked for long

**Visibility:**
- Health and the web UI show free space, oldest stored day, number of deleted files and the last deletion time
- every deletion is logged at `WARN` level

**Tests:**
- native tests of the deletion policy with a fake filesystem
- an HIL test with a card pre-filled with a dummy file up to the low watermark

### 4.6 Uplinks (data targets) and connectivity

```cpp
enum class UplinkResult : uint8_t { Sent, Busy, NoConnectivity, Rejected, TransientError };

class IUplink {
public:
  virtual const char* name() const = 0;              // "lorawan-cayenne", "osem-http", …
  virtual void begin(const UplinkConfig&) = 0;
  virtual void tick(uint32_t nowMs) = 0;             // drives its own state machine
  virtual bool wantsData(uint32_t nowMs) const = 0;  // interval elapsed & link ready
  virtual UplinkResult send(const Measurement* batch, uint8_t n, uint8_t& accepted) = 0;
  virtual UplinkStatus status() const = 0;           // for Health/UI
};
```

Data **target** and **transport** are separate:
- `LoRaWanCayenneUplink` → uses `LoRaWanLink` (MCCI LMIC) + `CayenneEncoder`.
- `OsemHttpUplink` → uses `IpConnectivity` (WiFi now, Ethernet later) + `OsemCsvEncoder` (same CSV format `sensorId,value[,timestamp]` as home v1, sent with an `Authorization` token header).
- `NoUplink`: an explicitly valid configuration (**SD-only station**).

**Answer to the open question "what if no connection is configured?":** that is a valid state. The station measures and stores on its SD card (always present). The status LED and web UI show "no data target configured". There is no hidden default target.

Several uplinks can be active at once (e.g. LoRa + WiFi in the future). Each has its own outbox cursor.

### 4.7 Cross-cutting services
- **Config** (see §5)
- **Logger:**
  - `LOGI("sps30", "fan cleaning started")`, with levels E/W/I/D and module tags
  - output to Serial (if enabled) and a small static **RAM ring buffer** (implemented: last 16 lines, ~2 KB; decision 2026-10-15: the log lives on the SD card, RAM only bridges the time before the card is mounted, the status snapshot and service mode). Console `log` reads the end of the current log file from the card
  - `/logs/B<boot>-<part>.log` on the SD card with size-based rotation (§4.5)
  - errors and warnings also go to a small persistent "last 20 errors" list in NVS
  - logs can be read via REST and are part of the diagnostic report
- **Clock:** UTC internally. Time sources by priority:
  1. LoRaWAN `DeviceTimeReq` (Olpe, see §6.1)
  2. NTP (later, when WiFi STA is used)
  3. time set from the browser when the service-mode UI is opened (phones always have the correct time)

  There is no external RTC. After a power loss the time is unknown until the first sync. See §6.1 for how measurements taken before then are handled.
- **Health/Diagnostics:** a central registry that every module reports to (`status`, `errorCount`, `lastError`, `lastSuccessTs`). It also collects:
  - firmware/hardware version, uptime, reset reason and boot count
  - free heap and minimum free heap, task stack high-water marks
  - I2C devices found, RSSI/SNR (WiFi and LoRa), uplink queue length

  It feeds the web UI and the **"Export diagnostic report"** JSON download.
- **Watchdog and recovery:**
  - The ESP task watchdog covers each task.
  - A supervisor checks that sensor/uplink loops make progress (heartbeat).
  - The reset reason (`esp_reset_reason()`) plus a "breadcrumb" (last module/state, kept in `RTC_NOINIT` memory) are written to NVS on the next boot.
  - The core dump partition is included in the diagnostic report.
  - **Crash-loop protection:** if the box crashes (panic, watchdog or brownout reset) more than 5 times in a row without reaching the "healthy for 10 min" mark, it starts in **safe mode**: no sensors/uplinks, only service mode/web UI, so it can still be repaired over the air. A normal start (power-on, flashing, a requested reboot) never counts and ends safe mode.
- **Board layer:** see §4.7.1. It provides one shared `I2CBus` object with a mutex, per-transaction timeout and bus recovery. The SPS30 needs ≤100 kHz, so the whole bus runs at 100 kHz.
- **Device identity (`factory_id` NVS partition):** serial number, hardware version, production batch, production date and calibration profile. These are written once at production by a provisioning script (`tools/provision.py`). This covers "issues f)" for now. If an EEPROM on the board/sensor appears later, a second identity source can be added.

#### 4.7.1 MCU S2 pins used (from [pins_senseBox_MCUS2.h](requirements/pins_senseBox_MCUS2.h))

| Function | Pins | Notes |
|---|---|---|
| Sensor I2C (QWIIC: AM2315C `0x38`, SPS30 `0x69`) | SDA 39, SCL 40 | `Wire`, 100 kHz; no address conflicts |
| LoRa-Bee (RFM95) in XB1 | CS 34, MOSI 35, SCK 36, MISO 37, RESET 38, INT 33, power `PIN_XB1_ENABLE` 41 | SPI bus shared with nothing else |
| SD card | CS 10, MOSI 11, SCK 12, MISO 13, power `SD_ENABLE` 9 | **own SPI bus** (separate from LoRa), so SD writes and LoRa TX cannot block each other. The power switch allows an SD power cycle to recover a hung card. |
| Status LED (NeoPixel) | 1 | |
| Service-mode button | BOOT, GPIO0 | strapping pin, see §4.8 |
| Second I2C (onboard MPU6050) | SDA 45, SCL 42 | not used for Olpe |
| UART port, PD sensor, USB | 43/44 (+ enable 26), 14/21, 19/20 | not used for Olpe; native USB stays free for logs/flashing |

**LoRa interrupt lines:** the SX1276 reports "TX/RX done" on DIO0 and "RX timeout" on DIO1, but the XB1 socket has only one interrupt pin (`INT` 33). Home v1 mapped both to that pin (`.dio = {PIN_XB1_INT, PIN_XB1_INT, …}`) and worked with LMIC. LMIC does this safely: it **polls** the pins and reads the radio's IRQ register to find out *which* event happened, so two signals on one pin don't matter to it.

We keep LMIC and the same pin mapping. Only the reset pin is new:

```cpp
const lmic_pinmap lmic_pins = {
  .nss  = PIN_XB1_CS,                                 // 34
  .rxtx = LMIC_UNUSED_PIN,
  .rst  = PIN_XB1_RESET,                              // 38 (unused on the SAMD board)
  .dio  = { PIN_XB1_INT, PIN_XB1_INT, LMIC_UNUSED_PIN }, // 33 = DIO0 + DIO1
};
```

The phase 0 spike still has to confirm this works on the ESP32-S2, since home v1 ran on the SAMD MCU. It tests:
- a failed join (the RX timeout must fire)
- an uplink without a downlink
- the `DeviceTimeReq` downlink

Also new compared to home v1: the MCU S2 has a radio **reset** pin (38) and a power switch for the XB1 socket (41). The firmware uses them to reset a hung radio without rebooting the box.

### 4.8 Service mode and web UI
- **Separate boot mode (decision 2026-10-14):** service mode and normal operation never run at the same time. Entering service mode restarts the box into a boot mode that only starts WiFi, the web server and the console; leaving it restarts into normal operation. Reason: the MCU S2 has no PSRAM, and service mode on top of normal operation ran out of RAM (WPA2 failed at 10 KB free, crashes at 15 KB). With separate modes each gets the whole RAM, and future features in normal operation (many sensors, SD, more uplinks) can't break the repair path. Cost: no measurements while servicing.
- **Status snapshot:** right before the restart, normal operation writes a snapshot to RTC memory (`StatusSnapshot`, 3.5 KB, survives the software restart, checksummed): overall health, every component, every sensor (state, readings, errors), the latest value per sensor/phenomenon, LoRaWAN (joined, uplinks, RSSI/SNR, airtime budget) and the last 12 log lines. Service mode shows it as "Status before service mode". After a normal start it is invalidated.
- **Entering:** **BOOT button (GPIO0)** held ≥ 3 s while the firmware runs (normal or safe mode). The LED turns purple; the snapshot is taken and the box restarts **when the button is released**, because restarting while GPIO0 is still held could start the ROM download mode. The request is one-shot (RTC memory), so the following boot is service mode and every later boot is normal.
  - ⚠️ GPIO0 is a strapping pin: if it is held **while the board powers up or resets**, the ESP32-S2 enters the ROM download (flashing) mode instead of starting the firmware. The user guide must say "press *after* the box has started". Firmware reads it with the internal pull-up and debounces in software; it must never be driven as an output.
- **Leaving (decision 2026-10-14):** any restart: the "Restart into normal mode" button in the web UI, `reboot` on the console, a power cycle, or the inactivity timeout. The BOOT button does nothing in service mode.
- **Access point:** `senseBox-<serial>` (or `senseBox-<last 3 MAC bytes>` without device identity), WPA2, max. 2 clients. The password defaults to `123sensebox` (decision 2026-10-14); a different one can be set with the console command `service-password` (stored as a secret in NVS, never shown in the web UI or the log). The default is not written to NVS, so a later firmware can change it for every box without its own password. The AP is **time-limited**: it closes (restart into normal mode) after `service.apTimeoutMin` (default 15) minutes without activity. Activity = a device joining the AP, page loads, user actions in the web UI (save, reload, upload, …) and console input; a phone that just stays connected, the UI's 30 s status poll and the OS's captive-portal probes do not count (decision 2026-10-15: otherwise a phone left on the AP or a forgotten tab kept service mode open forever). A captive DNS (polled in the main loop, no extra task) makes phones open the UI; it is also at `http://192.168.4.1/`. No mDNS (`sensebox.local`): it costs ~6 KB of internal RAM.
- **UI (offline, embedded, no CDNs/external fonts):** one page, `web/index.html` (plain HTML/CSS/JS), embedded into the firmware at build time. Layout (2026-10-15), top to bottom: service-mode card (timeout, restart button); expandable **Status** (saved snapshot: overview, sensors, last measured values, LoRaWAN, components); expandable **Logs** (most recent logs from the snapshot, downloadable as text; log files on the SD card); expandable **Settings** (name, DevEUI, JoinEUI, AppKey write-only; full config as JSON); **Firmware update**. Expandable cards are native `<details>` elements. No live log of service mode (decision 2026-10-15: not needed; still on the serial console).
- **REST API:**
  - `GET /api/status`: service info (SSID, clients, seconds until timeout, heap) + snapshot
  - `GET /api/log`: log lines before service mode (snapshot)
  - `GET /api/config`: stored config (no secrets), `appKeySet`, `differsFromRunning`; `POST /api/config`: complete config
  - `POST /api/config/field` `{"field":"name"|"uplinks.devEui"|"uplinks.joinEui","value":"…"}`; `POST /api/appkey` `{"appKey":"…"}`
  - `POST /api/reboot`
  - Later: OTA upload (phase 6), diagnostic report, sensor check, browser time → clock
- **SD log files (2026-10-15):** the web UI lists the newest 40 log files on the card and downloads them (`GET /api/sdlogs`, `GET /api/sdlogs/file?name=…`). The card is mounted only for the duration of such a request and unmounted afterwards (~5–9 KB heap; refused below 16 KB free), so service mode otherwise keeps its RAM for WiFi. The saved log before service mode can be downloaded as a text file (in the browser, no firmware cost). Not tested on hardware yet: heap during a download with a client connected.
- **Not implemented yet:** starting service mode by itself when the box is unconfigured or in safe mode; an on-demand sensor check (read each configured sensor once); the diagnostic report download.
- **USB mass-storage mode** for editing files on the SD card (idea from requirements.md): the ESP32-S2 has native USB, so this is technically possible via TinyUSB MSC. It's deferred: it conflicts with the firmware's own SD access, and the REST/UI path covers the use case.

### 4.9 OTA with rollback and compatibility check
- **Path for Olpe:** upload through the service-mode web UI. OTA over LoRaWAN is not realistic with the bandwidth and duty cycle. Remote OTA over WiFi (pull from a release server) is phase 8.
- **Implemented (2026-10-15)** — signatures and anti-downgrade are deferred (decision 2026-10-15):
  - **Upload:** service mode, web UI or `POST /api/ota` (multipart). It is streamed with `esp_ota_write` (sequential erase) into the inactive slot.
  - **Image marker instead of a descriptor struct:** every build contains the text marker `@@SBH2-IMAGE@@ fw=<version> hw=<list>;` (`lib/ota/src/ImageMarker.cpp`, `SBH_HW_COMPAT` build flag). The upload scans the stream for it (KMP scanner, chunk-independent).
  - **Checks, all before the boot partition is switched:** ESP32-S2 image header; marker present (otherwise "not a senseBox:home v2 firmware"); board hardware revision (from `factory_id`) in `hw`, any revision accepted while unknown; `esp_ota_end()` hash check.
  - **Self-test (`SelfTest.h`):** keep the image after 2 min of normal operation with the config accepted. Config rejected → explicit rollback; crash/watchdog/any restart before that → the bootloader rolls back. Sensors are deliberately not part of it, so a box with a broken sensor can still be updated.
  - **During the self-test:** the BOOT button doesn't enter service mode (that restart would roll back).
  - **Arduino hook:** the core's `verifyRollbackLater()` is overridden (`extern "C"`!).
  - **Build check:** `scripts/check_image.py` fails the build if the marker or the hook override is missing. Both had been silently missing in a first build: the marker was dropped by the linker, and the hook had C++ linkage.
- *Original design:* **Image metadata:** a custom descriptor struct in the app image holds the firmware version, a **list of compatible hardware revisions**, the minimum config schema version and the signature info. Before switching the boot partition, `OtaManager` checks:
  1. the signature
  2. that the current hardware revision (from `factory_id`) is in the list
  3. that the image is not older than `minAllowedVersion` (anti-downgrade, configurable)

  On a mismatch it aborts with a clear error message in the UI and log.
- **Rollback:** the new image boots in `PENDING_VERIFY` state. Within the first N minutes it runs a **self-test**: config loads, at least one configured sensor reaches `Ready` (or the sensors match their pre-update state), the uplink stack initialises, no watchdog resets. If the self-test passes, it calls `esp_ota_mark_app_valid_cancel_rollback()`. If it fails or there is a reset before then, the bootloader rolls back automatically. ⚠️ Verify that the Arduino-ESP32 3.x bootloader for the S2 is built with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. If not, ship a custom bootloader via pioarduino's `custom_sdkconfig` / hybrid compile.
- **Signing:** application-level signatures. Ed25519/ECDSA-P256 over the image hash, verified with mbedTLS before the switch. The public key is compiled in and the private key stays in CI / with the release manager. **No eFuses are burnt** (decision): no Secure Boot, no flash encryption.
  - What this protects: nobody can install a foreign image *through the OTA path* (web upload, later remote OTA).
  - What it does not protect: someone with physical USB access can still flash anything, and can read the flash (including NVS secrets). For outdoor boxes mounted at 2.5 m this is an accepted risk.

---

## 5. Configuration

- **Storage:** one versioned JSON document in NVS (`cfg/main`) plus **separate** NVS keys for secrets (`sec/lora_appkey`, later `sec/wifi_psk`, `sec/osem_token`). See §5.1.
- **Schema versioning:** `schemaVersion` sits at the top level. On boot, `ConfigMigrator` runs the chain `v1→v2→…→vN` (each step is a pure function, unit-tested with fixtures). If the migration fails, the old config is kept as `cfg/backup` and the box starts in safe mode instead of deleting anything.
- **Validation:** typed C++ structs (`StationConfig`) are filled from JSON after validation. The rest of the firmware only sees the structs, never JSON.
- **Import/export:**
  - REST/UI: full JSON without secrets
  - optional `/config/import.json` on SD: applied once at boot, then renamed to `.applied`. It is meant for bulk provisioning and **cannot contain secrets** (they are ignored if present).
  - serial provisioning (`tools/provision.py`) for production: writes `factory_id`, LoRaWAN keys and the initial config
- **Example config for an Olpe station:**
```json
{
  "schemaVersion": 1,
  "station": { "name": "Olpe-017", "timezone": "Europe/Berlin" },
  "sensors": [
    { "slot": 0, "type": "am2315c", "intervalSec": 60,
      "calibration": { "air_temperature": { "offset": 0.0, "gain": 1.0 } } },
    { "slot": 1, "type": "sps30",  "intervalSec": 60, "mode": "continuous",
      "fanCleaningIntervalDays": 4 }
  ],
  "storage": { "lowWatermarkPct": 10, "highWatermarkPct": 20, "minRetentionDays": 30 },
  "uplinks": [
    { "type": "lorawan-cayenne", "region": "EU868", "intervalSec": 600, "send": "aggregated",
      "channels": [
        { "ch": 1, "slot": 0, "phenomenon": "air_temperature",   "lpp": "temperature" },
        { "ch": 2, "slot": 0, "phenomenon": "relative_humidity", "lpp": "humidity" },
        { "ch": 3, "slot": 1, "phenomenon": "pm2_5",             "lpp": "analog_in" },
        { "ch": 4, "slot": 1, "phenomenon": "pm10",              "lpp": "analog_in" },
        { "ch": 5, "slot": 1, "phenomenon": "pm1",               "lpp": "analog_in" },
        { "ch": 6, "slot": 1, "phenomenon": "pm4",               "lpp": "analog_in" }
      ] }
  ],
  "service": { "apTimeoutMin": 15 },
  "deployment": { "lat": 51.03, "lon": 7.85, "heightAboveGround": 2.5, "shield": "senseBox Shield V2" }
}
```
(The LoRaWAN AppKey is stored as a secret, not in this document, see §5.1.)

### 5.1 Are LoRaWAN keys part of the config?

**They are provisioned together with the config, but stored and handled separately.**

| | DevEUI, JoinEUI | AppKey |
|---|---|---|
| Secret? | no (identifiers, printed on the label is fine) | **yes**: anyone holding it can impersonate the box on TTN |
| Stored in | `cfg/main` → `uplinks[].devEui / joinEui` | NVS `sec/lora_appkey` |
| In config export / diagnostic report | yes | **never**. Only "set / not set" is shown |
| Via REST/UI | read + write | write-only |
| Survives OTA / config migration | yes | yes (secrets namespace is never touched by migrations) |

Why keep the AppKey out of the config document?
- The config gets exported, attached to support tickets, compared between boxes and possibly imported from SD. A key inside it leaks sooner or later.
- With the keys outside, the 50 Olpe configs are **identical** except name, location and the two EUIs. One template plus a per-device CSV is enough.

Production flow (`tools/provision.py`):
1. `osem_ttn_setup.py` creates the OSeM box and the TTN device and writes one CSV row per unit: serial, DevEUI, JoinEUI, AppKey, OSeM box ID.
2. `provision.py <serial>` flashes the firmware and, over serial, writes:
   - `factory_id`
   - the config (template + row)
   - the AppKey into the secrets namespace
3. It then reads back the status ("AppKey set: yes") without ever reading the key itself.

Re-provisioning in the field goes through the service-mode UI (write-only field).

---

## 6. Olpe specifics: LoRaWAN + Cayenne LPP

### Payload
Cayenne LPP frame: `[channel][type][data]` per value.

| Ch | Value | LPP type | Size | Resolution / range |
|---|---|---|---|---|
| 1 | air temperature | Temperature `0x67` | 4 B | 0.1 °C, signed |
| 2 | relative humidity | Humidity `0x68` | 3 B | 0.5 % |
| 3 | PM2.5 | Analog Input `0x02` | 4 B | 0.01, **max 327.67 µg/m³** (clamped and flagged above) |
| 4 | PM10 | Analog Input `0x02` | 4 B | same |
| 5 | PM1 | Analog Input `0x02` | 4 B | same |
| 6 | PM4 | Analog Input `0x02` | 4 B | same |
| (later) | station status bitmask | Digital Input `0x00` / Analog | 3–4 B | sensor error bits, SD present, time synced |

Full frame: **23 B** (all four PM values, decision). A sensor in `ERROR` **omits** its channels. ⚠️ The OSeM integration currently turns a missing channel into the value **0** (see "Findings" below); fixing that is part of phase 4.

### Airtime / TTN fair use, which drives the default interval
TTN Fair Use allows about **30 s of airtime per device per day**. A 23 B payload (36 B PHY) takes roughly:

| SF | Airtime per uplink | Max uplinks/day | → min. interval |
|---|---|---|---|
| SF7 | ~80 ms | ~370 | ~4 min |
| SF8 | ~145 ms | ~200 | ~7 min |
| SF9 | ~270 ms | ~110 | ~13 min |
| SF12 | ~2.0 s | ~15 | ~96 min |

→ **Measure locally every 60 s and send aggregated means every 10 min by default**, with ADR enabled. The 10 min interval fits the budget up to **SF8**. Boxes that only reach a gateway at SF9 or higher will send less often (about every 13 min at SF9). Checking gateway coverage at the Olpe sites (e.g. TTN Mapper, or a test box) before installation is therefore worthwhile. The uplink interval is configurable. `LoRaWanLink` tracks its own airtime budget and stretches the interval automatically when the SF is high. All raw 60 s values stay on the SD card.

### 6.0 LMIC integration

- **Library and build:** `mcci-catena/MCCI LoRaWAN LMIC library` at a pinned version. It is configured by build flags instead of `lmic_project_config.h`: `-D ARDUINO_LMIC_PROJECT_CONFIG_H_SUPPRESS -D CFG_eu868=1 -D CFG_sx1276_radio=1 -D DISABLE_PING -D DISABLE_BEACONS -D LMIC_ENABLE_DeviceTimeReq=1`.
- **Dedicated LMIC task:** LMIC must have `os_runloop_once()` called very often, or it misses the receive windows ~1 s and ~2 s after sending.
  - It runs in its **own FreeRTOS task** with higher priority than the main loop, which calls `os_runloop_once()` and yields with a 1-tick delay.
  - LMIC is not thread-safe. **Every** LMIC call (`LMIC_setTxData2`, `LMIC_requestNetworkTime`, reset/rejoin) happens in this task. The rest of the firmware talks to it only through a FreeRTOS queue (frame to send in; events such as joined / TX complete / time received out).
  - Slow work (SD writes, web server) therefore can't delay the RX windows.
- **Timing tolerance:** `LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100)` widens the RX windows slightly. This is a common requirement on ESP32 to receive join-accepts reliably. Confirm in the spike.
- **Keys at runtime:** `os_getDevEui()`, `os_getArtEui()` and `os_getDevKey()` read from config/NVS, so no keys are compiled in.
  - LMIC expects DevEUI and JoinEUI in **LSB-first** byte order and the AppKey MSB-first. The config/provisioning tool stores everything MSB-first as TTN shows it, and the firmware reverses the EUIs in one place, with a unit test.
- **Rejoin after every boot (no session persistence):**
  - On mains power, reboots are rare: power cuts, OTA updates, watchdog resets. A join after each one costs a few seconds of airtime and is acceptable.
  - Crash-loop protection (safe mode, §4.7) stops a box in a reboot loop from joining endlessly.
  - Join retries use backoff: LMIC's built-in join retry with increasing spreading factor, plus our own limit, e.g. pause 30 min after N failed attempts. The measurement loop never waits for a join.
  - Saving the session across reboots is possible later (store the session keys and frame counters, `LMIC_setSession`) but is not needed for Olpe.
- **Duty cycle / fair use:** LMIC enforces the legal EU868 duty cycle itself. The TTN fair-use budget (30 s airtime/day) is tracked by `LoRaWanLink`, which computes each frame's airtime from SF and payload size and stretches the interval when needed (see the airtime table above).
- **Hung radio:** if no `EV_TXCOMPLETE` arrives within a timeout, the radio is reset (pin 38). If that doesn't help, the XB1 socket is power-cycled (pin 41), followed by `LMIC_reset()` and a rejoin.

### 6.1 Time sync over LoRaWAN (no RTC, no WiFi)

**Mechanism: the `DeviceTimeReq` MAC command (LoRaWAN 1.0.3+, supported by TTN):**

In LMIC this is `LMIC_requestNetworkTime(callback, ctx)` before an uplink. In the callback, `LMIC_getNetworkTimeReference()` returns the network's GPS time together with LMIC's local tick count at the end of the uplink (build flag `LMIC_ENABLE_DeviceTimeReq=1`).

1. Along with a normal data uplink, the box adds the MAC command `DeviceTimeReq`. It is 1 byte in the frame options and costs almost no extra airtime.
2. The network server knows exactly when the uplink ended, because the gateways timestamp it (GPS-synchronised).
3. In the receive window of that uplink (RX1 ≈ 1 s or RX2 ≈ 2 s after transmission), the server replies with `DeviceTimeAns`: the time *at the end of the uplink*, as **GPS epoch seconds + fractions of 1/256 s**.
4. The box knows how long ago its own TX ended (it timestamps "TX done" locally). It sets: `now = answer + (local time since TX end)`.
5. GPS time → UTC: `unix = gps + 315964800 − leapSeconds` (currently 18). LMIC returns GPS time, so our `Clock` does this conversion. The leap-second constant is compiled in; a new leap second would need a firmware update, and none is planned by the IERS so far.

**Accuracy:** typically well below one second, which is far better than needed for 60 s measurements.

**When it runs:**
- right after every join (i.e. after every power-up), on the first data uplink
- then every **72 hours** (decision). The ESP32's crystal drifts ~1–2 s per day while running on mains, so the clock stays within a few seconds between syncs, which is irrelevant at 60 s resolution.
- If no answer arrives (downlink lost), it retries on the next uplink. TTN fair use allows **10 downlinks per day**; the sync needs one every three days.

**Before the first sync (after power loss):**
- The time is unknown for the first few minutes: join + first uplink is usually 0–10 min, longer without gateway coverage. During this time, measurements are timestamped as `bootId + uptime` and flagged `Q_TIME_UNSYNCED`.
- They are kept in RAM, not written to the SD card yet: daily files need a date, and 2 000 records ≈ 5 h of data for our sensors.
- When the sync arrives, the timestamps are converted to UTC (`syncedTime − (uptimeAtSync − uptimeAtSample)`), the flag is cleared, and the records go to the SD card.
- If no sync arrives before the RAM buffer is full, the records go to `/data/unsynced/<bootId>.bin` with uptime timestamps. When a sync arrives later in the same boot, they are corrected. If the box reboots first, they stay flagged on the SD card and can be fixed offline: `bootId` and the uptime offsets are stored, and the server knows when the box rejoined.
- **Uploads don't depend on the time:** the OSeM TTN integration stamps Cayenne values with the *arrival time* of the uplink, so live data on OSeM is correct even before the first sync. The device clock matters for the SD records and the logs.

**Other time sources:** if the service-mode UI is opened, the browser's time is sent along and sets the clock if no LoRaWAN sync has happened yet. This is useful for setting up a box before it has a gateway. Watchdog/software resets keep the system time if the RTC timer survives the reset (⚠️ verify on ESP32-S2; otherwise store the last known time + uptime in `RTC_NOINIT` memory and restore it).

### 6.2 Catch-up after a LoRaWAN outage: not over LoRa

Cayenne frames carry no timestamp, and the OSeM integration uses the arrival time. A backlog sent after an outage would therefore appear on OSeM at the **wrong time**. So for the LoRaWAN uplink:
- After an outage, it sends only the **current** 10-minute aggregate; the outbox cursor jumps to the newest record.
- The gap stays visible on OSeM. The complete data is on the SD card, can be downloaded as CSV via the service-mode UI, and can be uploaded to OSeM by a tool (`tools/sd_backfill.py`, phase 8) using OSeM's timestamped CSV upload.
- The HTTP uplink (phase 8) sends timestamps and therefore *does* do full catch-up through the outbox.

### Network and openSenseMap integration
- OTAA, EU868. TTN devices are registered as **LoRaWAN 1.0.3** (the MAC version LMIC implements).
- Join state and last uplink/downlink RSSI/SNR go to Health.
- `tools/osem_ttn_setup.py` creates the 50 boxes on OSeM and their TTN devices, and outputs the provisioning CSV (§5.1).

#### Findings from the OSeM TTN integration source ([sensebox/ttn-osem-integration](https://github.com/sensebox/ttn-osem-integration), `lib/decoding/cayenne-lpp.js`, `index_v3.js`)
1. **OSeM does not decode Cayenne bytes itself.** It reads TTN's `uplink_message.decoded_payload`. So the TTN application's **uplink payload formatter must be set to "CayenneLPP"**. TTN then produces fields like `temperature_1`, `relative_humidity_2`, `analog_in_3`.
2. **`decodeOptions` format:** a list of `{ "decoder": "<lpp type name>", "channel": <n>, <sensor match> }`. OSeM looks up the field `<decoder>_<channel>`. The sensor match can be `sensor_id`, `sensor_title`, `sensor_type` or `sensor_unit`.
   → Using **`sensor_title`**, all 50 Olpe boxes can share **one identical** `decodeOptions` block, as long as the sensors on every box have the same titles. No per-box sensor IDs are needed.
   ```json
   [
     { "decoder": "temperature",       "channel": 1, "sensor_title": "Temperatur" },
     { "decoder": "relative_humidity", "channel": 2, "sensor_title": "rel. Luftfeuchte" },
     { "decoder": "analog_in",         "channel": 3, "sensor_title": "PM2.5" },
     { "decoder": "analog_in",         "channel": 4, "sensor_title": "PM10" },
     { "decoder": "analog_in",         "channel": 5, "sensor_title": "PM1" },
     { "decoder": "analog_in",         "channel": 6, "sensor_title": "PM4" }
   ]
   ```
   (Titles confirmed. OSeM compares them case-insensitively and takes the first match, so every title must be unique per box. `osem_ttn_setup.py` creates the boxes with exactly these titles.)
3. **Timestamp = gateway receive time** (`rx_metadata[0].time`, falling back to TTN's `received_at`). This confirms §6.2: no catch-up over LoRa.
4. **Missing channels become 0** (accepted for now). The decoder does `payload[...] || 0`. If the SPS30 fails and the firmware omits its channels, OSeM stores **0 µg/m³**. This is a known limitation for Olpe:
   - the SD data and the web UI show the real state (sensor `ERROR`, no values)
   - the support guide should mention it ("a run of exact zeros on OSeM = sensor failure")
   - the fix in ttn-osem-integration (skip a missing field instead of writing 0) is in phase 8
5. **Only types that TTN's CayenneLPP formatter knows can be used.** For PM this means `analog_in` (0.01 resolution, **max 327.67**). Above that, the firmware clamps the value and flags it, and the full value is on the SD card. SPS30 readings above 327 µg/m³ are rare outdoors (fireworks, construction dust next to the box).
   - Decision: `analog_in` with clamping.
6. GPS fields in the payload would be attached as location; we don't send any.
- Downlinks (later): a small command set on fPort 10: change the uplink interval, trigger rejoin, request a status frame. The design leaves room for this, but it is not required for Olpe.

### SPS30 operation
- 5 V supply via the adapter board, I2C at 100 kHz.
- Start the measurement, wait 30 s warm-up (`Q_WARMUP`), then poll `read_data_ready` every second, never block.
- Automatic fan cleaning every 4 days (configurable). Read and log the device status register (fan/laser errors).
- Optionally (phase 8): a duty-cycled mode (sleep between measurements) to extend lifetime and reduce power, with an adjusted warm-up.

### AM2315C (temperature / humidity)
[Adafruit 5182](https://www.adafruit.com/product/5182): an **AHT20** chip in a 98 mm encased probe on a 50 cm cable.
- **Specs:** I2C address `0x38` (fixed), 2.5–5.5 V, typical accuracy ±0.3 °C / ±2 % RH, range −40…80 °C. Not rated weatherproof, but much better suited to wind and rain than a bare breakout.
- **The cable is useful against the "temperatures too high" problem** (issues a): the probe can sit in the radiation shield, away from the heat of the MCU and the SPS30.
- **Driver:** the existing Adafruit library `adafruit/Adafruit AHTX0` (pinned version), wrapped in our `ISensor` interface. Registry entry `am2315c`; the same driver also serves plain AHT20 boards later.
  - The library's `getEvent()` **waits ~80 ms** for each measurement (it busy-polls the sensor). This is an accepted exception to the non-blocking rule: 80 ms once a minute in the main loop is harmless, and the wait is bounded (no endless loop). The driver says so in a comment:
    ```cpp
    // NOTE: Adafruit_AHTX0::getEvent() blocks ~80 ms while the AHT20 measures.
    // Accepted: bounded wait, once per interval. Do not call it more often than needed.
    ```
  - If `begin()` or `getEvent()` fails, the sensor goes to `ERROR` and is re-initialised by the SensorManager with backoff, as for any other sensor.
- **Measurement interval:** 60 s is far above the ≥ 2 s the manufacturer recommends to avoid self-heating.
- **No serial number and no heater:** the AHT20 has neither. For traceability (issues f), the probe's serial/batch is entered as instance metadata at provisioning.
- **Pull-ups and cable:** the probe has its own 4.7 kΩ pull-ups, in parallel with those on the MCU/adapter boards. That's fine at 100 kHz with a 50 cm cable; check the signal quality once with an oscilloscope in phase 2.
- **Accuracy:** typically ±0.3 °C, lower than the SHT45 (±0.1 °C). For a city monitoring network, a comparison measurement before rollout makes sense (see issues a).
- **Later drivers (phase 8):** SHT4x (`0x44`) and HDC1080 (`0x40`). Their addresses differ from the AM2315C's, so auto-detection can tell all three apart.

---

## 7. Failure catalogue and handling

Grouped list of the "standard failures" (as requested in olpe-specific.md). Each one must be (a) detected, (b) contained, (c) visible in Health/UI, and (d) covered by a test (unit, HIL or soak).

| Group | Failure | Detection | Handling |
|---|---|---|---|
| **Sensor** | not present at boot | probe fails | `ERROR`, retry with backoff, other sensors continue |
| | unplugged during operation | I2C NACK / timeout | same; error counter; re-init on reconnect |
| | returns error / CRC error | driver return code | retry once (`Q_COMM_RETRY`), then `ERROR` |
| | implausible / stuck / spiking values | QualityChecker | flag, keep the value, warn in UI |
| | SPS30 fan/laser failure | status register | `ERROR` + specific diagnostic message |
| | still warming up | driver state | `Q_WARMUP` |
| **Bus/Hardware** | I2C bus hangs (SDA held low) | transaction timeout | bus recovery (9 clocks); if it persists: error state + scheduled reboot |
| | LoRa radio hangs | no TX-done within timeout | radio reset (pin 38) / XB1 power cycle (pin 41), `LMIC_reset()` + rejoin |
| | brown-out / power loss | reset reason | log on next boot; data loss bounded by last flush |
| **Storage** | no SD card (hardware fault: the card is part of the hardware) | mount fails | `ERROR`, measurements buffered in RAM, retry with backoff |
| | SD removed during operation | write error | unmount, power-cycle, remount attempts; RAM buffer meanwhile |
| | SD over 90 % full | free space check | janitor deletes oldest files; `ERROR` if nothing is deletable |
| | SD corrupt / read-only | write/verify error | `ERROR`, retries; suggest replacing the card |
| | card smaller than 4 GB | size at mount | used, but flagged in status and log |
| | NVS full / corrupt | NVS API error | rebuild from backup / defaults → safe mode |
| **Connectivity** | LoRaWAN join fails / no gateway | join timeout | backoff, keep measuring, queue in outbox |
| | duty cycle / fair use limit | airtime budget | stretch interval |
| | WiFi unavailable / wrong password (later) | connect timeout / auth fail | backoff, outbox, status in UI |
| | LAN cable unplugged (later) | link status | same |
| | DNS / TLS / certificate failure (later) | HTTP client error | backoff, specific error in UI |
| **Backend** | OSeM unreachable / HTTP 5xx | response | outbox, retry later |
| | OSeM rejects data (4xx, bad token / sensor ID) | response | stop retrying the bad batch, persistent error in UI |
| **Time** | no time source yet | Clock state | uptime-based timestamps, `Q_TIME_UNSYNCED` |
| | NTP / DeviceTime unreachable for a long time | last sync age | warning, keep the drifting clock |
| **Config** | invalid / unknown config | validation | reject on PUT; at boot use last known good |
| | old schema after update | `schemaVersion` | migrate; on failure keep backup + safe mode |
| **Firmware** | hang / deadlock | task watchdog / heartbeat | reset, store breadcrumb |
| | crash loop | boot counter | safe mode (service UI only) |
| | broken OTA image | self-test fails / reset | automatic rollback |
| | incompatible hardware | image descriptor check | refuse the update, message in UI |
| **Memory** | heap fragmentation / leak | min free heap, largest free block trend | warning; planned reboot as a last resort (configurable) |
| **User** | service AP left open | inactivity timer | auto-close |

---

## 8. Repository layout (in [new/](new/))

```
new/
├── platformio.ini            envs: mcu_s2 (release), mcu_s2_debug, native (unit tests), mcu_s2_test (HIL)
├── partitions.csv
├── src/main.cpp              composition root only (~100 lines)
├── lib/                      each module is a PlatformIO library → enforced boundaries,
│   ├── core/                 Measurement, enums, RingBuffer, Result, Logger, Clock      (native-testable)
│   ├── config/               StationConfig, ConfigStore (NVS), ConfigMigrator, Secrets  (migrator native-testable)
│   ├── board/                MCU S2 pins, I2CBus, power control, button, StatusLed
│   ├── sensors/              ISensor, SensorDescriptor, DriverRegistry, SensorManager
│   │   └── drivers/          am2315c/ (Adafruit AHTX0), sps30/   (later sht4x/, hdc1080/, scd30/, rg15/, smt50/, …)
│   ├── pipeline/             QualityChecker, Calibration, Aggregator, DataRouter        (native-testable)
│   ├── storage/              IRecordStore, SdRecordStore, RamRecordStore, Outbox
│   ├── uplinks/              IUplink, lorawan/ (LoRaWanLink, CayenneEncoder),
│   │                         osem/ (OsemHttpUplink, OsemCsvEncoder), none/              (encoders native-testable)
│   ├── net/                  WifiManager (STA + service AP), mDNS, captive portal
│   ├── service/              WebServer, RestApi, ServiceMode, embedded UI header
│   ├── ota/                  OtaManager, ImageDescriptor, SignatureVerifier, SelfTest
│   └── health/               HealthRegistry, Watchdog/Supervisor, ResetInfo, DiagnosticReport
├── web/                      UI sources + build script → lib/service/generated/ui_bundle.h
├── test/
│   ├── native/               Unity tests for pure logic
│   └── embedded/             on-device tests (HIL)
├── tools/
│   ├── provision.py          write factory_id, secrets, initial config over serial
│   ├── osem_ttn_setup.py     create OSeM boxes + TTN devices, generate decodeOptions
│   ├── sign_firmware.py      sign release images
│   └── soak/                 scripts for the long-term test rig
└── docs/                     architecture, adding-a-sensor.md, adding-an-uplink.md, config schema
```

**Task model (ESP32-S2 is single-core, so keep it small):**
- `main` loop task: SensorManager + pipeline + storage. Everything is cooperative and non-blocking via `tick()`.
- `uplink` task: drives uplink state machines (when to send, building Cayenne frames, outbox). It talks to the main loop through the Outbox (mutex) and to the LMIC task through queues. Later, TLS handshakes for HTTP run here as well.
- `lmic` task: owns LMIC exclusively and calls `os_runloop_once()` continuously, with the highest priority of our tasks (§6.0).
- AsyncTCP / WiFi tasks (framework-owned), active only when WiFi or service mode is on.
- All tasks are registered with the task watchdog.

---

## 9. Phases and milestones

Every phase ends with something running on real hardware. Phases 0–7 make up the **Olpe release (v2.0)**.

### Phase 0: Foundations
- [x] Verify the remaining MCU S2 facts: 4 MB flash; PSRAM not present on every board
- [ ] Set up the PlatformIO project in `new/` with pioarduino / Arduino 3.x, envs, partition table, clang-format, CI (build + native tests)
- [ ] Spike: MCCI LMIC on the ESP32-S2 with the home v1 pin mapping (§4.7.1: failed join, uplink without downlink, DeviceTimeReq) + OTAA join + one Cayenne uplink from the MCU S2 to TTN → OSeM (cayenne-lpp). **Lowest-risk-first:** this proves the whole Olpe data path before building the architecture.
- [ ] Spike: confirm that the bootloader has rollback enabled
- **Exit:** "hello LoRa" frame visible on an OSeM test box.

### Phase 1: Core
- [ ] `core`: data model, ring buffer, logger with RAM ring, Clock (uptime + sync API)
- [ ] `config`: structs, NVS store, JSON import/export, migrator framework (v1 only, but tested with a fake v0→v1)
- [ ] `health`: registry, reset reason and breadcrumb, boot counter, task WDT, safe mode skeleton
- [ ] `board`: I2CBus with timeout and recovery, power control, StatusLed (NeoPixel status codes)
- **Exit:** the box boots, logs, persists config across reboots, recovers from a forced hang.

### Phase 2: Sensors and pipeline
- [ ] `ISensor`, registry, SensorManager (scheduling, timeouts, backoff, error counters)
- [ ] Drivers: AM2315C (Adafruit AHTX0 wrapper, ~80 ms blocking read noted in code), SPS30 (non-blocking, warm-up, fan cleaning, status register, serial number)
- [ ] QualityChecker, Calibration, Aggregator, DataRouter (+ native unit tests)
- **Exit:** unplugging or replugging any sensor at runtime doesn't stop the others, and a re-plugged sensor recovers automatically.

### Phase 3: Storage
- [x] SD history + log files, hot-plug, SD power-cycle recovery, janitor overflow protection (§4.5, §4.5.1) (2026-10-14)
- [ ] Test on hardware: 24 h with card pulled/reinserted, a card filled to the low watermark, mount time of a full 4 GB card (watchdog: 5 s)
- [ ] Outbox with persisted cursors (only with a catch-up uplink, phase 8)
- **Exit:** a 24 h run with the SD card pulled and reinserted shows no gaps for the time the card was present, the RAM buffer covers short outages, and the missing card is reported as `ERROR`.

### Phase 4: LoRaWAN Cayenne uplink
- [ ] `LoRaWanLink` (MCCI LMIC in its own task, queue interface, OTAA with join backoff, EUI byte-order handling, airtime budget, radio reset)
- [ ] Time sync via `DeviceTimeReq` (§6.1): sync after join + every 72 h, re-stamping of unsynced records, retry policy
- [ ] `CayenneEncoder` (native-tested, including clamping and omitted channels), `LoRaWanCayenneUplink` (current aggregate only, no backlog, §6.2)
- [ ] `tools/osem_ttn_setup.py`, shared `decodeOptions` block (by `sensor_title`), TTN application with CayenneLPP formatter
- **Exit:** 3 test boxes deliver to OSeM for 72 h. After a power cut, the SD records get correct UTC timestamps once the box has rejoined. During a gateway outage, the SD card keeps recording without gaps.

### Phase 5: Service mode and web UI
- [x] Separate boot mode, BOOT long press, status snapshot, AP with captive DNS, inactivity timeout (2026-10-14)
- [x] REST API (status, config, secrets write-only, logs, reboot)
- [x] Web UI (status before service mode, settings, config JSON, logs), embedded at build time
- [ ] Test on hardware: free heap with AP up and two clients, captive portal on Android/iOS
- [ ] Auto-start when unconfigured / in safe mode, sensor check, diagnostic report, browser time → Clock, firmware page (with phase 6)
- **Exit:** a non-developer can set up LoRa keys and diagnose a pulled sensor from a phone, without a laptop.

### Phase 6: OTA
- [x] OTA via web upload, image marker with HW compatibility list, self-test + rollback (2026-10-15)
- [ ] Test on hardware: update app0 → app1 → app0, a deliberately crashing image rolls back, a foreign .bin is refused, config and SD data survive
- [ ] Signature verification, anti-downgrade (deferred, decision 2026-10-15)
- [ ] `tools/sign_firmware.py`, release process in CI
- **Exit:** a deliberately broken image rolls back by itself, an incompatible image is refused, and config survives the update.

### Phase 7: Olpe hardening and production
- [ ] HIL test rig: MCU S2 + sensors + switchable power (relay/USB hub control from a Raspberry Pi), automated scenarios from §7
- [ ] **7-day soak test** with ≥ 3 boxes outdoors: gateway outages, sensor unplugging, power loss, full SD card. Track min free heap and reset counts.
- [ ] Production: `provision.py` flow for 50 units (flash, factory_id, keys, config, label with serial + AP password), end-of-line self-test
- [ ] Docs: installation/mounting guide hooks, support guide ("how to read the diagnostic report")
- **Exit:** release v2.0.0 signed and flashed onto the Olpe boxes.

### Phase 8+: After Olpe (in rough priority order)
1. WiFi STA + `OsemHttpUplink` (HTTPS, CSV with timestamps, batch catch-up), NTP. This replaces the `homev2_wifi` / `home_wifi_feinstaub` templates.
2. WebSocket live values in the UI
3. More sensor drivers (BMP280/DPS310, TSL45315/LTR329, VEML6070, SCD30, RG-15, SMT50, sound level, wind), ported from `old/` as drivers
4. Remote OTA over WiFi (signed release manifest)
5. Ethernet (W5500 bee) as a second `IpConnectivity`
6. Generic HTTP/MQTT uplink for custom endpoints
7. LoRaWAN downlink commands, status frame
8. Metadata model sync with openSenseMap / openSenseLab (SOSA/SSN)
9. Battery / low-power mode (deep sleep, duty-cycled SPS30), together with the BMS hardware
10. 30-day soak test as a standing release gate
11. SHT4x driver (the HDC1080 driver already exists, added 2026-10-13 for bench testing) and `tools/sd_backfill.py` (upload SD data to fill OSeM gaps)
12. Optional: LoRaWAN session persistence across reboots, or a switch to RadioLib (session persistence built in, SX126x radios)
13. Fix in `ttn-osem-integration`: missing Cayenne channel → no value instead of 0 (§6, finding 4)

---

## 10. Testing strategy

| Level | What | How |
|---|---|---|
| Unit (native) | data model, QualityChecker, Aggregator, Calibration, Cayenne/CSV encoders, ConfigMigrator, Outbox cursor logic, retry/backoff policy | `pio test -e native` (Unity), in CI on every push |
| Driver tests | each driver against a **fake I2C bus** (scripted responses, NACKs, timeouts) | native, via an `II2CBus` interface |
| On-device | NVS, SD, I2C bus recovery, OTA flow, watchdog | `pio test -e mcu_s2_test` on a board connected to CI runner / lab PC |
| HIL | failure scenarios from §7 with real sensors, power relays and a LoRa gateway | Python harness on a Raspberry Pi, nightly |
| Soak | 7 days (Olpe gate), later 30 days | outdoor test boxes; metrics come from the diagnostics endpoint + OSeM data completeness |

Architecture review checklist for every PR (from olpe-specific.md):
1. Does this need a new firmware variant? → it should not.
2. Does this module know a concrete sensor unnecessarily? → it should not.
3. What happens if this fails? → the rest keeps running.
4. Does this setting survive an update? → yes.
5. Can the user see this error in the web UI? → yes.

Plus: no heap allocation in the measurement path, no blocking wait without a timeout.

---

## 11. Open questions

Resolved on 2026-10-13: see §1.1.

Still open:
1. **SD card model:** 4 GB is decided. We should still pick an industrial / high-endurance model for the outdoor temperature range.
2. **To verify on hardware:** LMIC on the ESP32-S2 with the shared DIO0/DIO1 pin and `LMIC_setClockError`, whether the system time survives software resets. (Flash 4 MB and bootloader rollback support are confirmed; PSRAM is not present on every board.)
3. **Gateway coverage** at the Olpe sites: are there TTN gateways close enough for SF7–SF8?
