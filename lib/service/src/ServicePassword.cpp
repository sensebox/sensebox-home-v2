#include "ServicePassword.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "PersistentStore.h"

namespace servicepw {
namespace {

bool valid(const char* pw, char* err, size_t errLen) {
  const size_t len = pw != nullptr ? strlen(pw) : 0;
  if (len < kMinLen || len > kMaxLen) {
    snprintf(err, errLen, "password must have %u to %u characters", static_cast<unsigned>(kMinLen),
             static_cast<unsigned>(kMaxLen));
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if (pw[i] < 0x20 || pw[i] > 0x7E) {
      snprintf(err, errLen, "password must be printable ASCII");
      return false;
    }
  }
  if (pw[0] == ' ' || pw[len - 1] == ' ') {
    snprintf(err, errLen, "password must not start or end with a space");
    return false;
  }
  return true;
}

}  // namespace

bool isSet() { return persist::hasSecret(persist::secret::kServicePassword); }

bool get(char* out, size_t cap, bool& isDefault) {
  isDefault = false;
  if (cap <= kMaxLen) return false;
  const size_t len =
      persist::readSecret(persist::secret::kServicePassword, reinterpret_cast<uint8_t*>(out), cap - 1);
  out[len] = '\0';
  char err[64];
  if (len > 0 && valid(out, err, sizeof(err))) return true;

  // Nothing (valid) stored: the default. Not written to NVS, so changing kDefault in a later
  // firmware applies to every box that never had its own password.
  strncpy(out, kDefault, cap - 1);
  out[cap - 1] = '\0';
  isDefault = true;
  return true;
}

bool set(const char* password, char* err, size_t errLen) {
  if (!valid(password, err, errLen)) return false;
  if (!persist::setSecret(persist::secret::kServicePassword, reinterpret_cast<const uint8_t*>(password),
                          strlen(password))) {
    snprintf(err, errLen, "could not store password");
    return false;
  }
  return true;
}

}  // namespace servicepw
