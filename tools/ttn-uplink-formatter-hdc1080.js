// TTN v3 uplink payload formatter for senseBox:home v2 "lorawan-compact" frames, HDC1080 layout
// (config/examples/hdc1080-compact.json). Same as ttn-uplink-formatter.js except LAYOUT.
// TTN console: Payload formatters -> Uplink -> Formatter type "Custom Javascript formatter".
//
// Frame layout v1 (fPort 2, big endian, see lib/uplinks/src/CompactEncoder.h):
//   bytes 0-3  window end, Unix seconds; 0 = the box had no time yet (use TTN's received_at)
//   then       one value per entry of the box config's uplink `channels`, in the same order:
//                air_temperature    int16   0.01 °C    missing = 0x8000
//                relative_humidity  uint8   0.5 %      missing = 0xFF
//                pm1/pm2_5/pm4/pm10 uint16  0.1 µg/m³  missing = 0xFFFF
//
// LAYOUT must list the same phenomena in the same order as `channels` in the box config.
// `field` is the name in the decoded payload. The names match TTN's CayenneLPP formatter,
// so the openSenseMap `cayenne-lpp` profile works with unchanged decodeOptions.
var LAYOUT = [
  { phenomenon: "air_temperature", field: "temperature_1" },
  { phenomenon: "relative_humidity", field: "relative_humidity_2" }
];

var CODECS = {
  air_temperature: { size: 2, signed: true, factor: 100 },
  relative_humidity: { size: 1, signed: false, factor: 2 },
  pm1: { size: 2, signed: false, factor: 10 },
  pm2_5: { size: 2, signed: false, factor: 10 },
  pm4: { size: 2, signed: false, factor: 10 },
  pm10: { size: 2, signed: false, factor: 10 }
};

function decodeUplink(input) {
  var b = input.bytes;
  if (input.fPort !== 2) {
    return { errors: ["unexpected fPort " + input.fPort + ", compact layout v1 uses port 2"] };
  }
  var expected = 4;
  for (var i = 0; i < LAYOUT.length; i++) {
    var codec = CODECS[LAYOUT[i].phenomenon];
    if (!codec) return { errors: ["LAYOUT: unknown phenomenon " + LAYOUT[i].phenomenon] };
    expected += codec.size;
  }
  if (b.length !== expected) {
    return { errors: ["frame has " + b.length + " bytes, LAYOUT expects " + expected + ": does it match the box config?"] };
  }

  var data = {};
  var t = ((b[0] << 24) >>> 0) + (b[1] << 16) + (b[2] << 8) + b[3];
  if (t !== 0) {
    data.unixtime = t;
    data.measured_at = new Date(t * 1000).toISOString();
  }

  var pos = 4;
  for (var j = 0; j < LAYOUT.length; j++) {
    var c = CODECS[LAYOUT[j].phenomenon];
    var raw = c.size === 2 ? (b[pos] << 8) | b[pos + 1] : b[pos];
    pos += c.size;
    var bits = 8 * c.size;
    var missing = c.signed ? 1 << (bits - 1) : (1 << bits) - 1;
    if (raw === missing) continue;  // sensor without a value in this window
    if (c.signed && raw & (1 << (bits - 1))) raw -= 1 << bits;
    data[LAYOUT[j].field] = raw / c.factor;
  }
  return { data: data };
}
