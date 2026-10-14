#pragma once

#include <stddef.h>
#include <stdint.h>

// NVS-backed persistence (plan §5, §5.1). Three separate areas:
//   "cfg" namespace  - the station config JSON (survives OTA, migrated on load)
//   "sec" namespace  - secrets (LoRaWAN AppKey, ...). Never exported, never logged.
//   "sys" namespace  - small system counters (boot guard)
//   factory_id partition - device identity written once in production
//
// Secret values are only read by the module that needs them (e.g. the LoRaWAN stack).
// Everything user-facing only gets hasSecret().

namespace persist {

bool begin();

// --- configuration --------------------------------------------------------------
// Returns the length of the stored JSON (0 if none or it doesn't fit `cap`).
size_t loadConfigJson(char* buf, size_t cap);
bool saveConfigJson(const char* json);
// Keeps the previous config as backup (used when a migration or validation fails).
bool backupConfigJson(const char* json);

// --- secrets ----------------------------------------------------------------------
namespace secret {
constexpr const char* kLoRaAppKey = "lora_appkey";       // 16 bytes
constexpr const char* kServicePassword = "service_pw";  // 8-63 ASCII characters (WPA2)
}
bool setSecret(const char* key, const uint8_t* data, size_t len);
bool hasSecret(const char* key);
size_t readSecret(const char* key, uint8_t* out, size_t cap);

// --- system counters ----------------------------------------------------------------
uint8_t loadUnhealthyBoots();
void saveUnhealthyBoots(uint8_t value);

// --- device identity (factory_id partition, read-only for the firmware) -------------
struct DeviceIdentity {
  char serial[24];
  char hardware[16];
  char batch[24];
  char productionDate[12];
  char calibrationProfile[24];
};
// Fills `out` with what is present; missing fields are empty. False if the partition is empty.
bool loadIdentity(DeviceIdentity& out);

}  // namespace persist
