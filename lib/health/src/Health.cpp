#include "Health.h"

#include <string.h>

namespace {
constexpr uint32_t kLockTimeoutMs = 20;

uint8_t severity(ComponentStatus s) {
  switch (s) {
    case ComponentStatus::Ok: return 1;
    case ComponentStatus::Degraded: return 2;
    case ComponentStatus::Error: return 3;
    default: return 0;
  }
}
}  // namespace

const char* componentStatusName(ComponentStatus s) {
  switch (s) {
    case ComponentStatus::Unknown: return "unknown";
    case ComponentStatus::Ok: return "ok";
    case ComponentStatus::Degraded: return "degraded";
    case ComponentStatus::Error: return "error";
    case ComponentStatus::Disabled: return "disabled";
  }
  return "unknown";
}

HealthRegistry::Id HealthRegistry::add(const char* name, ComponentStatus initial) {
  MutexLock lock(mutex_, kLockTimeoutMs);
  if (!lock.locked()) return kInvalid;
  for (size_t i = 0; i < count_; ++i) {
    if (strncmp(items_[i].name, name, ComponentHealth::kNameLen - 1) == 0) return static_cast<Id>(i);
  }
  if (count_ >= kMaxComponents) return kInvalid;
  ComponentHealth& c = items_[count_];
  c = ComponentHealth{};
  strncpy(c.name, name, ComponentHealth::kNameLen - 1);
  c.status = initial;
  return static_cast<Id>(count_++);
}

void HealthRegistry::setStatus(Id id, ComponentStatus status) {
  MutexLock lock(mutex_, kLockTimeoutMs);
  if (!lock.locked() || !valid(id)) return;
  items_[id].status = status;
}

void HealthRegistry::reportError(Id id, int32_t code, const char* message, ComponentStatus status) {
  MutexLock lock(mutex_, kLockTimeoutMs);
  if (!lock.locked() || !valid(id)) return;
  ComponentHealth& c = items_[id];
  c.status = status;
  ++c.errorCount;
  c.lastErrorCode = code;
  strncpy(c.lastError, message != nullptr ? message : "", ComponentHealth::kErrorLen - 1);
  c.lastError[ComponentHealth::kErrorLen - 1] = '\0';
  c.lastErrorUptimeS = uptimeS_();
}

void HealthRegistry::reportSuccess(Id id, uint32_t utc) {
  MutexLock lock(mutex_, kLockTimeoutMs);
  if (!lock.locked() || !valid(id)) return;
  ComponentHealth& c = items_[id];
  c.status = ComponentStatus::Ok;
  c.lastSuccessUptimeS = uptimeS_();
  if (utc != 0) c.lastSuccessUtc = utc;
}

bool HealthRegistry::get(Id id, ComponentHealth& out) const {
  MutexLock lock(mutex_, kLockTimeoutMs);
  if (!lock.locked() || !valid(id)) return false;
  out = items_[id];
  return true;
}

ComponentStatus HealthRegistry::overall() const {
  MutexLock lock(mutex_, kLockTimeoutMs);
  if (!lock.locked()) return ComponentStatus::Unknown;
  ComponentStatus worst = ComponentStatus::Unknown;
  for (size_t i = 0; i < count_; ++i) {
    if (severity(items_[i].status) > severity(worst)) worst = items_[i].status;
  }
  return worst;
}
