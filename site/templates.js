// Templates for the config generator, the same as the files in config/ (checked by
// tools/test/templates.test.mjs). Built in so the page works without fetching anything.
const TEMPLATES = [
  {
    id: "olpe",
    source: "config/olpe-template.json",
    label: "Olpe: AM2315C + SPS30, LoRaWAN compact",
    config: {"schemaVersion":1,"station":{"name":"","timezone":"Europe/Berlin"},"sensors":[{"slot":0,"type":"am2315c","intervalSec":60},{"slot":1,"type":"sps30","intervalSec":60,"fanCleaningIntervalDays":4}],"uplinks":[{"type":"lorawan-compact","devEui":"","joinEui":"0000000000000000","intervalSec":600,"timeSyncIntervalSec":259200,"channels":[{"slot":0,"phenomenon":"air_temperature"},{"slot":0,"phenomenon":"relative_humidity"},{"slot":1,"phenomenon":"pm2_5"},{"slot":1,"phenomenon":"pm10"},{"slot":1,"phenomenon":"pm1"},{"slot":1,"phenomenon":"pm4"}]}],"storage":{"lowWatermarkPct":10,"highWatermarkPct":20,"minRetentionDays":30},"service":{"apTimeoutMin":15},"deployment":{}},
  },
  {
    id: "hdc1080-compact",
    source: "config/examples/hdc1080-compact.json",
    label: "HDC1080, LoRaWAN compact",
    config: {"schemaVersion":1,"station":{"name":"","timezone":"Europe/Berlin"},"sensors":[{"slot":0,"type":"hdc1080","intervalSec":60}],"uplinks":[{"type":"lorawan-compact","devEui":"","joinEui":"0000000000000000","intervalSec":600,"timeSyncIntervalSec":259200,"channels":[{"slot":0,"phenomenon":"air_temperature"},{"slot":0,"phenomenon":"relative_humidity"}]}]},
  },
  {
    id: "hdc1080-sps30-lorawan",
    source: "config/examples/hdc1080-sps30-lorawan.json",
    label: "HDC1080 + SPS30, LoRaWAN Cayenne",
    config: {"schemaVersion":1,"station":{"name":""},"sensors":[{"slot":0,"type":"hdc1080","intervalSec":60},{"slot":1,"type":"sps30","intervalSec":60,"fanCleaningIntervalDays":4}],"uplinks":[{"type":"lorawan-cayenne","devEui":"","joinEui":"","intervalSec":600,"channels":[{"ch":1,"slot":0,"phenomenon":"air_temperature","lpp":"temperature"},{"ch":2,"slot":0,"phenomenon":"relative_humidity","lpp":"relative_humidity"},{"ch":3,"slot":1,"phenomenon":"pm2_5","lpp":"analog_in"},{"ch":4,"slot":1,"phenomenon":"pm10","lpp":"analog_in"},{"ch":5,"slot":1,"phenomenon":"pm1","lpp":"analog_in"},{"ch":6,"slot":1,"phenomenon":"pm4","lpp":"analog_in"}]}]},
  },
  {
    id: "hdc1080-lorawan",
    source: "config/examples/hdc1080-lorawan.json",
    label: "HDC1080, LoRaWAN Cayenne",
    config: {"schemaVersion":1,"station":{"name":""},"sensors":[{"slot":0,"type":"hdc1080","intervalSec":60}],"uplinks":[{"type":"lorawan-cayenne","devEui":"","joinEui":"","intervalSec":600,"channels":[{"ch":1,"slot":0,"phenomenon":"air_temperature","lpp":"temperature"},{"ch":2,"slot":0,"phenomenon":"relative_humidity","lpp":"relative_humidity"}]}]},
  },
  {
    id: "am2315c-lorawan",
    source: "config/examples/am2315c-lorawan.json",
    label: "AM2315C, LoRaWAN Cayenne",
    config: {"schemaVersion":1,"station":{"name":""},"sensors":[{"slot":0,"type":"am2315c","intervalSec":60}],"uplinks":[{"type":"lorawan-cayenne","devEui":"","joinEui":"","intervalSec":600,"channels":[{"ch":1,"slot":0,"phenomenon":"air_temperature","lpp":"temperature"},{"ch":2,"slot":0,"phenomenon":"relative_humidity","lpp":"relative_humidity"}]}]},
  },
  {
    id: "hdc1080",
    source: "config/examples/hdc1080.json",
    label: "HDC1080 only, no upload (SD card)",
    config: {"schemaVersion":1,"station":{"name":""},"sensors":[{"slot":0,"type":"hdc1080","intervalSec":60}]},
  },
];
