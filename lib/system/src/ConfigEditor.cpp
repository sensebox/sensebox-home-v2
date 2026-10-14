#include "ConfigEditor.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ConfigCodec.h"
#include "ConfigEdit.h"
#include "Log.h"
#include "PersistentStore.h"
#include "StatusSnapshot.h"
#include "SystemInfo.h"

namespace cfgedit {
namespace {

constexpr const char* kTag = "config";

struct Scratch {
  char json[kJsonCap];
  config::StationConfig edit;
};

std::unique_ptr<Scratch> makeScratch(char* err, size_t errLen) {
  auto s = sysinfo::makeTemp<Scratch>();
  if (!s) snprintf(err, errLen, "out of memory");
  return s;
}

// Loads the stored config into s.edit; if nothing is stored, copies `running`.
bool loadStored(Scratch& s, const config::StationConfig& running, char* err, size_t errLen) {
  const size_t len = persist::loadConfigJson(s.json, sizeof(s.json));
  if (len == 0) {
    s.edit = running;
    return true;
  }
  const config::DecodeResult r = config::decode(s.json, len, s.edit);
  if (!r.ok) {
    snprintf(err, errLen, "stored config is invalid (%s); store a complete config first", r.error);
    return false;
  }
  return true;
}

// Encodes s.edit, re-decodes it (full validation) and stores it.
bool store(Scratch& s, char* err, size_t errLen) {
  const size_t len = config::encode(s.edit, s.json, sizeof(s.json));
  if (len == 0) {
    snprintf(err, errLen, "config too large");
    return false;
  }
  // decode() only writes its output on success, so decoding into s.edit itself is safe.
  const config::DecodeResult r = config::decode(s.json, len, s.edit);
  if (!r.ok) {
    snprintf(err, errLen, "%s", r.error);
    return false;
  }
  if (!persist::saveConfigJson(s.json)) {
    snprintf(err, errLen, "could not store config");
    return false;
  }
  return true;
}

bool parseHex(const char* hex, uint8_t* out, size_t bytes) {
  if (strlen(hex) != bytes * 2) return false;
  for (size_t i = 0; i < bytes; ++i) {
    char pair[3] = {hex[2 * i], hex[2 * i + 1], '\0'};
    if (!isxdigit(static_cast<unsigned char>(pair[0])) || !isxdigit(static_cast<unsigned char>(pair[1]))) return false;
    out[i] = static_cast<uint8_t>(strtoul(pair, nullptr, 16));
  }
  return true;
}

}  // namespace

bool storeJson(const char* json, size_t len, char* err, size_t errLen) {
  auto s = makeScratch(err, errLen);
  if (!s) return false;
  const config::DecodeResult r = config::decode(json, len, s->edit);
  if (!r.ok) {
    snprintf(err, errLen, "%s", r.error);
    return false;
  }
  if (!store(*s, err, errLen)) return false;
  LOGI(kTag, "new config stored");
  return true;
}

bool setField(const char* group, const char* key, const char* value, const config::StationConfig& running,
              char* err, size_t errLen) {
  const config::Field field = config::fieldFromPath(group, key);
  if (field == config::Field::Unknown) {
    snprintf(err, errLen, "unknown field");
    return false;
  }
  auto s = makeScratch(err, errLen);
  if (!s) return false;
  if (!loadStored(*s, running, err, errLen)) return false;
  if (!config::setField(s->edit, field, value, err, errLen)) return false;
  if (!store(*s, err, errLen)) return false;
  LOGI(kTag, "config field %s%s%s updated", group != nullptr ? group : "", group != nullptr ? "." : "", key);
  return true;
}

bool setAppKeyHex(const char* hex, char* err, size_t errLen) {
  uint8_t key[16];
  if (hex == nullptr || !parseHex(hex, key, sizeof(key))) {
    snprintf(err, errLen, "AppKey must be 32 hex digits");
    return false;
  }
  const bool ok = persist::setSecret(persist::secret::kLoRaAppKey, key, sizeof(key));
  memset(key, 0, sizeof(key));
  if (!ok) {
    snprintf(err, errLen, "could not store AppKey");
    return false;
  }
  LOGI(kTag, "LoRaWAN AppKey updated");
  return true;
}

size_t storedJson(const config::StationConfig& running, char* buf, size_t cap, bool pretty, bool& differs, char* err,
                  size_t errLen) {
  auto s = makeScratch(err, errLen);
  if (!s) return 0;
  if (!loadStored(*s, running, err, errLen)) return 0;
  // Compare via checksums of the compact encodings, so no second output buffer is needed.
  size_t len = config::encode(running, buf, cap);
  const uint32_t runningCrc = len > 0 ? snapshot::crc32(buf, len) : 0;
  len = config::encode(s->edit, buf, cap);
  if (len == 0) {
    snprintf(err, errLen, "config too large for output buffer");
    return 0;
  }
  differs = runningCrc != snapshot::crc32(buf, len);
  if (pretty) len = config::encode(s->edit, buf, cap, true);
  return len;
}

}  // namespace cfgedit
