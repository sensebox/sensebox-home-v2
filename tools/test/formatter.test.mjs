// Tests for the TTN uplink formatters (node --test tools/test). The formatters are plain TTN
// scripts without exports, so each one is evaluated in its own context, like TTN does.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";

function load(file) {
  const code = readFileSync(new URL("../" + file, import.meta.url), "utf8");
  return vm.runInNewContext(code + "\ndecodeUplink;");
}

const hex = (s) => Array.from(Buffer.from(s.replace(/\s/g, ""), "hex"));

const olpe = load("ttn-uplink-formatter.js");
const hdc1080 = load("ttn-uplink-formatter-hdc1080.js");

// Same plain-object comparison as JSON, since the vm context has its own Object prototype.
const plain = (v) => JSON.parse(JSON.stringify(v));

test("olpe: README example frame", () => {
  const r = olpe({ fPort: 2, bytes: hex("68E8F79808EE78007B009600510084") });
  assert.deepEqual(plain(r), {
    data: {
      unixtime: 1760098200,
      measured_at: "2025-10-10T12:10:00.000Z",
      temperature_1: 22.86,
      relative_humidity_2: 60,
      analog_in_3: 12.3,
      analog_in_4: 15,
      analog_in_5: 8.1,
      analog_in_6: 13.2,
    },
  });
});

test("olpe: no time yet, missing values and negative temperature", () => {
  // t = 0, temp -5.5 °C (0xFDDA), rh missing, pm2_5 missing, pm10 0, pm1 missing, pm4 6553.4
  const r = olpe({ fPort: 2, bytes: hex("00000000 FDDA FF FFFF 0000 FFFF FFFE") });
  assert.deepEqual(plain(r), { data: { temperature_1: -5.5, analog_in_4: 0, analog_in_6: 6553.4 } });
});

test("olpe: wrong port and wrong length are errors", () => {
  assert.ok(olpe({ fPort: 1, bytes: hex("68E8F79808EE78007B009600510084") }).errors);
  assert.ok(olpe({ fPort: 2, bytes: hex("68E8F79808EE78") }).errors);
});

test("hdc1080: 7-byte frame", () => {
  const r = hdc1080({ fPort: 2, bytes: hex("68E8F79808EE78") });
  assert.deepEqual(plain(r), {
    data: {
      unixtime: 1760098200,
      measured_at: "2025-10-10T12:10:00.000Z",
      temperature_1: 22.86,
      relative_humidity_2: 60,
    },
  });
  assert.ok(hdc1080({ fPort: 2, bytes: hex("68E8F79808EE78007B009600510084") }).errors);
});
