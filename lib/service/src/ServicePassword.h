#pragma once

#include <stddef.h>

// WPA2 password of the service-mode access point (plan §4.8, §5.1). Defaults to kDefault; a
// different one can be set with the console command `service-password` (stored as a secret in
// NVS). It is never shown in the web UI or the log.

namespace servicepw {

constexpr size_t kMinLen = 8;   // WPA2 limits
constexpr size_t kMaxLen = 63;
constexpr const char* kDefault = "123sensebox";

// Copies the password into `out` (terminated): the stored one, or kDefault if none is set.
// `isDefault` tells which. Returns false only if `cap` is too small.
bool get(char* out, size_t cap, bool& isDefault);

// Sets a new password (8-63 printable ASCII characters, no spaces at the ends).
bool set(const char* password, char* err, size_t errLen);

bool isSet();

}  // namespace servicepw
