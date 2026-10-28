# TTN uplink formatter for `lorawan-compact`

Worked examples for [ttn-uplink-formatter.js](ttn-uplink-formatter.js) (Olpe) and
[ttn-uplink-formatter-hdc1080.js](ttn-uplink-formatter-hdc1080.js). The encoder side is
[lib/uplinks/src/CompactEncoder.h](../lib/uplinks/src/CompactEncoder.h); the setup in TTN is in the
[README](../README.md#compact-frames-lorawan-compact). The same frames are checked by
[test/formatter.test.mjs](test/formatter.test.mjs) (`node --test tools/test`).

## Why there is a `LAYOUT`

A compact frame has no channel or type bytes, only the window end followed by the values. The
formatter can't tell from the bytes which value is which, so `LAYOUT` says it:

```js
var LAYOUT = [
  { phenomenon: "air_temperature",   field: "temperature_1" },
  { phenomenon: "relative_humidity", field: "relative_humidity_2" },
  { phenomenon: "pm2_5",             field: "analog_in_3" },
  { phenomenon: "pm10",              field: "analog_in_4" },
  { phenomenon: "pm1",               field: "analog_in_5" },
  { phenomenon: "pm4",               field: "analog_in_6" }
];
```

- **Order = frame layout.** Same phenomena, same order as the uplink `channels` in the box config
  (e.g. [config/examples/olpe.json](../config/examples/olpe.json)). The `slot` doesn't matter, only
  the position.
- **`phenomenon`** picks the codec from `CODECS` (size, signed, factor, "missing" marker):

  | Phenomenon | Bytes | Encoding | Factor | Missing |
  |---|---|---|---|---|
  | `air_temperature` | 2 | `int16` | ÷ 100 (0.01 °C) | `0x8000` |
  | `relative_humidity` | 1 | `uint8` | ÷ 2 (0.5 %) | `0xFF` |
  | `pm1`, `pm2_5`, `pm4`, `pm10` | 2 | `uint16` | ÷ 10 (0.1 µg/m³) | `0xFFFF` |

- **`field`** is the key in the decoded payload. The names follow TTN's CayenneLPP formatter, so the
  openSenseMap `cayenne-lpp` profile works with unchanged `decodeOptions`.

The configurator ([site/app.js](../site/app.js), `renderFormatter`) generates `LAYOUT` from the
config's `channels` (`field` = `<lpp name>_<position>`) and swaps it into the formatter.

If `LAYOUT` and the config disagree in length, the formatter returns
`frame has N bytes, LAYOUT expects M`. If they only disagree in order, the length matches and the
values land in the wrong fields, with no error.

## Example 1: Olpe, normal frame

FPort `2`, payload `68E8F79808EE78007B009600510084` (15 bytes = 4 + 2 + 1 + 2 + 2 + 2 + 2).

| Bytes | Hex | Raw | LAYOUT entry | Value | Field |
|---|---|---|---|---|---|
| 0–3 | `68 E8 F7 98` | 1760098200 | (window end) | 2025-10-10 12:10:00 UTC | `unixtime`, `measured_at` |
| 4–5 | `08 EE` | 2286 | `air_temperature` | 22.86 °C | `temperature_1` |
| 6 | `78` | 120 | `relative_humidity` | 60 % | `relative_humidity_2` |
| 7–8 | `00 7B` | 123 | `pm2_5` | 12.3 µg/m³ | `analog_in_3` |
| 9–10 | `00 96` | 150 | `pm10` | 15.0 µg/m³ | `analog_in_4` |
| 11–12 | `00 51` | 81 | `pm1` | 8.1 µg/m³ | `analog_in_5` |
| 13–14 | `00 84` | 132 | `pm4` | 13.2 µg/m³ | `analog_in_6` |

Decoded:

```json
{
  "unixtime": 1760098200,
  "measured_at": "2025-10-10T12:10:00.000Z",
  "temperature_1": 22.86,
  "relative_humidity_2": 60,
  "analog_in_3": 12.3,
  "analog_in_4": 15,
  "analog_in_5": 8.1,
  "analog_in_6": 13.2
}
```

## Example 2: Olpe, no time yet, missing values, negative temperature

FPort `2`, payload `00000000FDDAFFFFFF0000FFFFFFFE`.

| Bytes | Hex | Raw | LAYOUT entry | Value | Field |
|---|---|---|---|---|---|
| 0–3 | `00 00 00 00` | 0 | (window end) | box had no time yet | none (use TTN's `received_at`) |
| 4–5 | `FD DA` | 64986 → −550 (signed) | `air_temperature` | −5.5 °C | `temperature_1` |
| 6 | `FF` | missing | `relative_humidity` | – | none |
| 7–8 | `FF FF` | missing | `pm2_5` | – | none |
| 9–10 | `00 00` | 0 | `pm10` | 0 µg/m³ | `analog_in_4` |
| 11–12 | `FF FF` | missing | `pm1` | – | none |
| 13–14 | `FF FE` | 65534 | `pm4` | 6553.4 µg/m³ (maximum) | `analog_in_6` |

Decoded:

```json
{ "temperature_1": -5.5, "analog_in_4": 0, "analog_in_6": 6553.4 }
```

## Example 3: HDC1080 only

[ttn-uplink-formatter-hdc1080.js](ttn-uplink-formatter-hdc1080.js) has only temperature and
humidity in `LAYOUT`, so a frame is 7 bytes. FPort `2`, payload `68E8F79808EE78` (the first 7 bytes
of example 1):

```json
{
  "unixtime": 1760098200,
  "measured_at": "2025-10-10T12:10:00.000Z",
  "temperature_1": 22.86,
  "relative_humidity_2": 60
}
```

If you send the 15-byte Olpe frame to this formatter, the length check fails and it returns an error.
