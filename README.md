# senseBox:home v2 firmware

Firmware for the senseBox:home v2: an environmental measurement station based on the senseBox MCU S2 (ESP32-S2).

There is **one firmware for all stations**. Which sensors are connected, how often they measure and where the data goes are set in a **configuration** stored on the box, not compiled into the firmware. Switching a sensor (e.g. HDC1080 → AM2315C) or a data target means changing the config, not flashing a different firmware.

## Quickstart: HDC1080 only

The smallest setup: one HDC1080 on the MCU S2, no SD card, no WiFi, no LoRaWAN. The box measures and shows the values on the serial console.

**You need:**
- senseBox MCU S2 with USB cable
- HDC1080 temperature/humidity sensor on a QWIIC/I2C port
- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension)

**1. Build and flash the development firmware.** The dev build logs every single measurement; the normal build only logs the averages that get uploaded.

```sh
pio run -e mcu_s2_dev -t upload
pio device monitor
```

If the upload doesn't find the board: hold the BOOT button, press and release RESET, release BOOT, and upload again.

**2. Set the config.** Paste this as one line into the serial monitor and press Enter:

```
config-set {"schemaVersion":1,"station":{"name":"my-test-box"},"sensors":[{"slot":0,"type":"hdc1080","intervalSec":60}]}
```

Answer: `ok: config stored, reboot to apply`. Then:

```
reboot
```

**3. Watch the values.** After the restart the log shows a measurement every 60 seconds:

```
[     61] D data     slot 0 air_temperature   raw          23.41 °C q=0x040
[     61] D data     slot 0 relative_humidity raw          56.20 % q=0x040
```

`q=0x040` is the quality flag "time not synced" (see [Quality flags](#quality-flags)). Without LoRaWAN the box has no time source, so timestamps are uptime seconds.

**4. Check the status:**

```
status
```

The `sensor 0 hdc1080 ready ...` line shows the sensor state, the number of readings and errors. Unplug the sensor to see it go to `error` with a retry countdown, and recover by itself when you plug it back in.

**Next step:** LoRaWAN upload, see [Adding LoRaWAN](#adding-lorawan-ttn).

---

## Serial console

Open the serial monitor at 115200 baud (`pio device monitor`). Commands are one line each, terminated with Enter. `help` lists them.

| Command | What it does |
|---|---|
| `help` | List the commands. |
| `status` | Firmware version, normal/safe/service mode, uptime, reset reason, time sync, memory, the health of every component, every sensor (state, readings, errors, retry countdown) and LoRaWAN (joined, AppKey set/missing, uplinks, downlinks, spreading factor, RSSI/SNR, remaining airtime budget). |
| `config` | Print the **stored** config as JSON. Secrets (the AppKey) are never included. If it differs from the running config, a note says a reboot is pending. |
| `config-set <json>` | Replace the whole config with a JSON document on one line (see [Configuration](#configuration)). It is validated first; if anything is wrong you get `error: ...` and nothing is stored. |
| `config-set name <name>` | Set the station name (max. 31 characters, spaces allowed). |
| `config-set uplinks devEui <hex>` | Set the LoRaWAN DevEUI: 16 hex digits, MSB first, exactly as shown in the TTN console. Upper/lower case and separators (`70 B3 D5 …`, `70:B3:…`) are accepted. Needs a `lorawan-cayenne` uplink in the config. |
| `config-set uplinks joinEui <hex>` | Set the LoRaWAN JoinEUI (also accepted: `appEui`). Same format as the DevEUI. |
| `appkey <hex>` | Store the LoRaWAN AppKey: 32 hex digits, MSB first, as in the TTN console. It is stored separately from the config and never printed; `status` only shows `set` or `missing`. |
| `service-password <password>` | Set the WiFi password of [service mode](#service-mode) (8–63 characters; default `123sensebox`). Stored like the AppKey and never printed by `status` or `config`. |
| `log` | Print the last 40 log lines. |
| `reboot` | Restart the box into normal operation. Also ends safe mode and service mode. |

Notes:
- **`config-set` and `appkey` take effect after `reboot`.** Several `config-set` commands before one reboot add up: each edit starts from the stored config.
- **Field names are case-insensitive** (`devEUI` = `devEui`).
- **Lines can be up to 2047 characters** (a complete config is about 1000). If a pasted line arrives incomplete, the console says so and doesn't run it.
- **The console also works in service mode**, e.g. to change settings or to `reboot`. There, `status` prints the status saved before service mode.

### Log format

```
[ uptime s] L tag      message
[    600] I cayenne  queued 23 bytes, 6 channels, time sync requested
```

`L` is the level: `E` error, `W` warning, `I` info, `D` debug. Debug lines only appear in the dev build (`mcu_s2_dev`).

### Status LED

| LED | Meaning |
|---|---|
| 🔵 - blue | starting |
| 🟢 - short green flash every 5 s | everything OK |
| 🟠 - orange flash every 2 s | degraded: something needs attention (e.g. not joined yet, a window without valid values) |
| 🔴 - red flash every second | error: a component failed (e.g. a sensor) |
| 🟣 - purple | [service mode](#service-mode): BOOT held ≥ 3 s (the box restarts when you release it), and while service mode runs |
| 🔴🟣 - alternating red/purple | If the box crashes (panic, watchdog, brownout) more than 5 times in a row without running normally for 10 minutes, it starts in **safe mode**. Sensors and LoRaWAN stay off and only the console runs, so a broken config can't keep the box in a reboot loop. Service mode can be started from safe mode with the BOOT button. |

---

## Service mode

Service mode opens a WiFi access point with a web page for checking and setting up the box from a phone or laptop, without a USB cable.

The MCU S2 has no PSRAM, and WiFi needs a large part of the RAM. So service mode **replaces** normal operation: while it runs, nothing is measured and nothing is sent over LoRaWAN. Before switching, the box saves its current status, and the web page shows that saved status.

**Start:**
1. Hold the **BOOT** button for 3 seconds while the box is running. The LED turns purple.
2. Release the button. The box saves its status and restarts into service mode.

Press BOOT only *after* the box has started. Held during power-on or reset, it puts the chip into flashing mode instead.

**Connect:**
1. Join the WiFi network `senseBox-…` (the serial number, or the last 6 digits of the MAC address). The password is **`123sensebox`**, unless a different one was set with `service-password` on the serial console. The serial console also shows the network name and password when service mode starts (`*** service mode: WiFi "…", password "…" ***`).
2. The web page usually opens by itself; otherwise open `http://192.168.4.1/`.

**The web page shows:**
- the status saved before service mode: overall health, sensors, latest values, LoRaWAN and components
- settings: station name, DevEUI, JoinEUI and AppKey (the AppKey can only be written, never read)
- the complete config as JSON, to edit
- the log before and during service mode
- a **Restart into normal mode** button

Changes are stored immediately and take effect after the restart.

**Leave:** any restart ends service mode:
- the restart button on the web page
- `reboot` on the serial console
- unplugging the power
- automatically after `service.apTimeoutMin` minutes (default 15) without page requests and without a connected device

Pressing BOOT again does nothing in service mode.

The web page uses a small REST API, which tools can use too:

| Request | What it does |
|---|---|
| `GET /api/status` | service-mode info (network, clients, time until timeout, memory) and the saved status |
| `GET /api/log` | log lines before and during service mode |
| `GET /api/config` | stored config (no secrets), whether the AppKey is set, whether a restart is pending |
| `POST /api/config` | store a complete config (JSON body) |
| `POST /api/config/field` | `{"field": "name" \| "uplinks.devEui" \| "uplinks.joinEui", "value": "…"}` |
| `POST /api/appkey` | `{"appKey": "<32 hex digits>"}` |
| `POST /api/reboot` | restart into normal operation |

All answers are JSON; errors come as `{"ok": false, "error": "…"}`.

---

## Configuration

The config is a JSON document stored in the box's flash memory. It survives firmware updates. When the format changes in a future version, an old config is migrated automatically (`schemaVersion`).

Set it with `config-set <json>` (one line) or field by field (see [Serial console](#serial-console)), and read it back with `config`.

All sections are optional; missing fields use the defaults below. Unknown fields are rejected, so typos don't go unnoticed.

```json
{
  "schemaVersion": 1,
  "station": { "name": "Olpe-017", "timezone": "Europe/Berlin" },
  "sensors": [
    { "slot": 0, "type": "am2315c", "intervalSec": 60,
      "calibration": { "air_temperature": { "gain": 1.0, "offset": -0.3 } } },
    { "slot": 1, "type": "sps30", "intervalSec": 60, "fanCleaningIntervalDays": 4 }
  ],
  "uplinks": [
    { "type": "lorawan-cayenne", "devEui": "70B3D57ED0001234", "joinEui": "0000000000000000",
      "intervalSec": 600, "timeSyncIntervalSec": 259200,
      "channels": [
        { "ch": 1, "slot": 0, "phenomenon": "air_temperature",   "lpp": "temperature" },
        { "ch": 2, "slot": 0, "phenomenon": "relative_humidity", "lpp": "relative_humidity" },
        { "ch": 3, "slot": 1, "phenomenon": "pm2_5", "lpp": "analog_in" },
        { "ch": 4, "slot": 1, "phenomenon": "pm10",  "lpp": "analog_in" },
        { "ch": 5, "slot": 1, "phenomenon": "pm1",   "lpp": "analog_in" },
        { "ch": 6, "slot": 1, "phenomenon": "pm4",   "lpp": "analog_in" }
      ] }
  ],
  "storage": { "sd": "auto", "lowWatermarkPct": 10, "highWatermarkPct": 20, "minRetentionDays": 30 },
  "service": { "apTimeoutMin": 15 },
  "deployment": { "lat": 51.03, "lon": 7.85, "heightAboveGround": 2.5 }
}
```

### Top level

| Field | Required | Description |
|---|---|---|
| `schemaVersion` | **yes** | Version of the config format. Currently `1`. |
| `station` | no | Station details, see below. |
| `sensors` | no | List of connected sensors (max. 8). Empty = the box measures nothing. |
| `uplinks` | no | List of data targets (max. 2). Empty = nothing is sent; the box only measures. |
| `storage` | no | SD card settings. *Not active yet.* |
| `service` | no | [Service mode](#service-mode) settings. |
| `deployment` | no | Free JSON object for installation metadata (location, mounting height, shield, …), max. 511 characters. Stored and exported, not interpreted by the firmware. |

### `station`

| Field | Default | Description |
|---|---|---|
| `name` | `""` | Station name, max. 31 characters. |
| `timezone` | `"Europe/Berlin"` | Only for display. The firmware works in UTC internally. |

### `sensors[]`

| Field | Default | Description |
|---|---|---|
| `slot` | **required** | Number of this sensor, `0`–`7`, unique. Uplink channels refer to sensors by slot. |
| `type` | **required** | Driver: `hdc1080`, `am2315c` or `sps30` (see table below). An unknown type shows up as an `ERROR` sensor in `status`. |
| `enabled` | `true` | `false` keeps the sensor in the config but doesn't start it. |
| `intervalSec` | `60` | Measurement interval in seconds, `1`–`86400`. |
| `calibration` | none | Per phenomenon: `{ "<phenomenon>": { "gain": 1.0, "offset": 0.0 } }`. Produces an additional *corrected* value `raw × gain + offset`; the raw value is kept. If present, averages and uploads use the corrected value. Max. 4 entries. |
| *other numbers* | | Driver-specific parameters, e.g. `fanCleaningIntervalDays` for the SPS30. Max. 4 per sensor. |

**Sensor types:**

| `type` | Sensor | I2C address | Phenomena | Parameters |
|---|---|---|---|---|
| `hdc1080` | TI HDC1080 (senseBox temperature/humidity board) | `0x40` | `air_temperature`, `relative_humidity` | none |
| `am2315c` | Adafruit AM2315C (encased AHT20 probe) | `0x38` | `air_temperature`, `relative_humidity` | none |
| `sps30` | Sensirion SPS30 particulate matter sensor | `0x69` | `pm1`, `pm2_5`, `pm4`, `pm10` | `fanCleaningIntervalDays` (default `4`): automatic fan cleaning interval |

All sensors use the QWIIC/I2C bus (SDA 39, SCL 40) at 100 kHz. The SPS30 needs 30 s to warm up after starting; values from that time are not used.

**Phenomena** (used in `calibration` and in uplink `channels`):

| Name | Unit |
|---|---|
| `air_temperature` | °C |
| `relative_humidity` | % |
| `pm1`, `pm2_5`, `pm4`, `pm10` | µg/m³ |

### `uplinks[]`

Currently one type: `lorawan-cayenne`, which sends averages over LoRaWAN, encoded as Cayenne LPP.

| Field | Default | Description |
|---|---|---|
| `type` | **required** | `lorawan-cayenne` (or `none`). |
| `devEui` | `""` | DevEUI from TTN: 16 hex digits, MSB first. Empty = LoRaWAN stays off. |
| `joinEui` | `""` | JoinEUI / AppEUI from TTN, same format. |
| `intervalSec` | `600` | Averaging window and send interval in seconds, `60`–`86400`. Each window's average of every channel is sent once. |
| `timeSyncIntervalSec` | `259200` (72 h) | How often the box asks the network for the time (LoRaWAN `DeviceTimeReq`), `3600`–`2592000`. It always asks after joining. |
| `channels` | none | Which value is sent on which Cayenne channel, max. 16. See below. |

The **AppKey** is not part of the config: it's a secret and is set with `appkey <hex>`.

**`channels[]`:**

| Field | Description |
|---|---|
| `ch` | Cayenne channel number, `0`–`255`, unique. Must match the openSenseMap `decodeOptions`. |
| `slot` | Sensor slot the value comes from; must be a configured sensor. |
| `phenomenon` | Which value of that sensor, e.g. `pm2_5`. |
| `lpp` | Cayenne type: `temperature` (0.1 °C), `relative_humidity` (0.5 %), `analog_in` (0.01, range ±327.67, used for PM), `luminosity`, `barometric_pressure`, `digital_in`. |

How sending works:
- **Frames are sent on LoRaWAN port 1.** The full Olpe frame is 23 bytes.
- **Values outside a Cayenne type's range are clamped and flagged;** e.g. PM above 327.67 µg/m³ is sent as 327.67.
- **A sensor without valid values in a window is left out of the frame.** Note that the openSenseMap then stores `0` for its channels.
- **Missed windows are not sent later** (e.g. while not joined): the openSenseMap would timestamp them with the arrival time.
- **TTN's fair-use limit of 30 s airtime per day is tracked.** When the budget is used up (slow spreading factors, short intervals), windows are skipped and logged.

### `storage` *(not active yet)*

| Field | Default | Description |
|---|---|---|
| `sd` | `"auto"` | `"auto"`: use an SD card if present; `"off"`: never. |
| `lowWatermarkPct` | `10` | Start deleting the oldest data when free space drops below this (%). |
| `highWatermarkPct` | `20` | Stop deleting when this much space is free (%). Must be above `lowWatermarkPct`. |
| `minRetentionDays` | `30` | Never delete data younger than this. |

### `service`

| Field | Default | Description |
|---|---|---|
| `apTimeoutMin` | `15` | Service mode ends (restart into normal operation) after this many minutes without page requests and without a connected device, `1`–`240`. |

### Quality flags

Every value carries a quality bitmask (`q=0x...` in the log). `0x000` means no remarks.

| Bit | Hex | Meaning | Excluded from averages |
|---|---|---|---|
| 0 | `0x001` | outside the sensor's physical range | yes |
| 1 | `0x002` | outside the plausible environmental range | yes |
| 2 | `0x004` | spike: changed faster than plausible since the last value | yes |
| 3 | `0x008` | stuck: identical value many times in a row | yes |
| 4 | `0x010` | sensor still warming up | yes |
| 5 | `0x020` | value obtained after a bus retry | no |
| 6 | `0x040` | time not synced: timestamp is seconds since boot, not UTC | no |
| 7 | `0x080` | average built from fewer samples than expected | no |
| 8 | `0x100` | value clamped to fit the transmission format | no |

---

## Adding LoRaWAN (TTN)

1. **In the TTN console,** create an end device with these settings:
   - Frequency plan: EU868
   - LoRaWAN version: **1.0.3**
   - Activation: OTAA
2. **Set the TTN uplink payload formatter** (application or device) to **CayenneLPP**.
3. **Put the TTN keys into the box** via the console:
   ```
   config-set {"schemaVersion":1,"station":{"name":"my-test-box"},"sensors":[{"slot":0,"type":"hdc1080","intervalSec":60}],"uplinks":[{"type":"lorawan-cayenne","intervalSec":600,"channels":[{"ch":1,"slot":0,"phenomenon":"air_temperature","lpp":"temperature"},{"ch":2,"slot":0,"phenomenon":"relative_humidity","lpp":"relative_humidity"}]}]}
   config-set uplinks devEui <DevEUI from TTN>
   config-set uplinks joinEui <JoinEUI from TTN>
   appkey <AppKey from TTN>
   reboot
   ```
4. **Check that it works:**
   - The log shows `joining` and then `joined`.
   - After 10 minutes, `queued 7 bytes`.
   - TTN shows `temperature_1` and `relative_humidity_2`.

To forward the data to the **openSenseMap**:
- In TTN, add a custom webhook: JSON format, base URL `https://ttn.opensensemap.org/v3`, only "Uplink message" enabled.
- On the openSenseMap box, set the TTN application ID and device ID, the decoding profile **Cayenne LPP**, and `decodeOptions` that map each channel to a sensor title. For example:

```json
[
  { "decoder": "temperature",       "channel": 1, "sensor_title": "Temperatur" },
  { "decoder": "relative_humidity", "channel": 2, "sensor_title": "rel. Luftfeuchte" }
]
```

`decodeOptions` is an openSenseMap setting, not part of the box config. It must use the same channel numbers as the box's `channels`.

---

## Development

PlatformIO environments:

| Env | Purpose |
|---|---|
| `mcu_s2` | Release firmware. |
| `mcu_s2_dev` | Debug logging; uses [config/olpe-template.json](config/olpe-template.json) when no config is stored. |
| `native` | Unit tests on the PC: `pio test -e native` |

Code layout and the rules for contributing are in [implementation-docs/CLAUDE.md](implementation-docs/CLAUDE.md).
