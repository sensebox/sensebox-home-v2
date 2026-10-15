# Example configs

Ready-made configs for common station setups. All fields are described in the main [README](../../README.md#configuration).

| File | Sensors | Upload |
|---|---|---|
| [hdc1080.json](hdc1080.json) | HDC1080 | none, measures only |
| [hdc1080-lorawan.json](hdc1080-lorawan.json) | HDC1080 | LoRaWAN, ch 1 temperature, ch 2 humidity |
| [am2315c-lorawan.json](am2315c-lorawan.json) | AM2315C | LoRaWAN, ch 1 temperature, ch 2 humidity |
| [hdc1080-sps30-lorawan.json](hdc1080-sps30-lorawan.json) | HDC1080, SPS30 | LoRaWAN, ch 1–2 as above, ch 3–6 PM2.5, PM10, PM1, PM4 |
| [olpe.json](olpe.json) | AM2315C, SPS30 | LoRaWAN, same channels as above (Olpe stations) |

The station name, DevEUI and JoinEUI are left empty (LoRaWAN stays off until they are set), so every file can be stored as is. `olpe.json` keeps the Olpe JoinEUI `0000000000000000`.

## Applying a config

`config-set` takes the JSON on one line. To turn a file into the command:

```sh
echo "config-set $(jq -c . config/examples/hdc1080-lorawan.json)"
```

Paste the output into the serial monitor, then set the per-station values:

```
config-set name <station name>
config-set uplinks devEui <DevEUI from TTN>
config-set uplinks joinEui <JoinEUI from TTN>
appkey <AppKey from TTN>
reboot
```

Alternatively, paste the file content into the config editor of the [service mode](../../README.md#service-mode) web page.

For LoRaWAN, the openSenseMap `decodeOptions` must use the same channel numbers as `channels` (see [Adding LoRaWAN](../../README.md#adding-lorawan-ttn)).
