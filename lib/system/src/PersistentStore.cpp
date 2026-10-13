#include "PersistentStore.h"

#include <Preferences.h>
#include <string.h>

#include "Log.h"

namespace persist {
namespace {

constexpr const char* kTag = "nvs";
constexpr const char* kCfgNs = "cfg";
constexpr const char* kSecNs = "sec";
constexpr const char* kSysNs = "sys";
constexpr const char* kFactoryPartition = "factory_id";
constexpr const char* kFactoryNs = "identity";

// Opens a namespace for the lifetime of the object.
class Ns {
 public:
  Ns(const char* name, bool readOnly, const char* partition = nullptr) { ok_ = prefs_.begin(name, readOnly, partition); }
  ~Ns() {
    if (ok_) prefs_.end();
  }
  bool ok() const { return ok_; }
  Preferences& operator*() { return prefs_; }
  Preferences* operator->() { return &prefs_; }

 private:
  Preferences prefs_;
  bool ok_ = false;
};

void readField(Preferences& p, const char* key, char* dst, size_t cap) {
  dst[0] = '\0';
  if (p.isKey(key)) p.getString(key, dst, cap);
}

}  // namespace

bool begin() {
  // Creating the namespaces once makes later read-only opens succeed on a fresh device.
  Ns cfg(kCfgNs, false);
  Ns sec(kSecNs, false);
  Ns sys(kSysNs, false);
  if (!cfg.ok() || !sec.ok() || !sys.ok()) {
    LOGE(kTag, "NVS not available");
    return false;
  }
  return true;
}

size_t loadConfigJson(char* buf, size_t cap) {
  Ns ns(kCfgNs, true);
  if (!ns.ok() || !ns->isKey("json")) return 0;
  const size_t len = ns->getStringLength("json");
  if (len == 0 || len >= cap) {
    if (len >= cap) LOGE(kTag, "stored config (%u bytes) larger than buffer", static_cast<unsigned>(len));
    return 0;
  }
  return ns->getString("json", buf, cap) > 0 ? strlen(buf) : 0;
}

bool saveConfigJson(const char* json) {
  Ns ns(kCfgNs, false);
  return ns.ok() && ns->putString("json", json) == strlen(json);
}

bool backupConfigJson(const char* json) {
  Ns ns(kCfgNs, false);
  return ns.ok() && ns->putString("backup", json) == strlen(json);
}

bool setSecret(const char* key, const uint8_t* data, size_t len) {
  Ns ns(kSecNs, false);
  return ns.ok() && ns->putBytes(key, data, len) == len;
}

bool hasSecret(const char* key) {
  Ns ns(kSecNs, true);
  return ns.ok() && ns->isKey(key) && ns->getBytesLength(key) > 0;
}

size_t readSecret(const char* key, uint8_t* out, size_t cap) {
  Ns ns(kSecNs, true);
  if (!ns.ok() || !ns->isKey(key)) return 0;
  const size_t len = ns->getBytesLength(key);
  if (len > cap) return 0;
  return ns->getBytes(key, out, cap);
}

uint8_t loadUnhealthyBoots() {
  Ns ns(kSysNs, true);
  return ns.ok() ? ns->getUChar("unhealthy", 0) : 0;
}

void saveUnhealthyBoots(uint8_t value) {
  Ns ns(kSysNs, false);
  if (ns.ok()) ns->putUChar("unhealthy", value);
}

bool loadIdentity(DeviceIdentity& out) {
  out = DeviceIdentity{};
  Ns ns(kFactoryNs, true, kFactoryPartition);
  if (!ns.ok()) return false;
  readField(*ns, "serial", out.serial, sizeof(out.serial));
  readField(*ns, "hw", out.hardware, sizeof(out.hardware));
  readField(*ns, "batch", out.batch, sizeof(out.batch));
  readField(*ns, "date", out.productionDate, sizeof(out.productionDate));
  readField(*ns, "calib", out.calibrationProfile, sizeof(out.calibrationProfile));
  return out.serial[0] != '\0';
}

}  // namespace persist
