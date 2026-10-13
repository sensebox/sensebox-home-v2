#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Mutex.h"

// Central health registry (plan §4.7). Every module registers a component and reports its
// status here. The web UI, the status LED and the diagnostic report read from it.

enum class ComponentStatus : uint8_t { Unknown = 0, Ok, Degraded, Error, Disabled };

const char* componentStatusName(ComponentStatus s);

struct ComponentHealth {
  static constexpr size_t kNameLen = 24;
  static constexpr size_t kErrorLen = 64;

  char name[kNameLen];
  ComponentStatus status;
  uint32_t errorCount;      // cumulative since boot
  int32_t lastErrorCode;
  char lastError[kErrorLen];
  uint32_t lastErrorUptimeS;
  uint32_t lastSuccessUptimeS;  // 0 = never
  uint32_t lastSuccessUtc;      // 0 = never or clock unsynced at that time
};

class HealthRegistry {
 public:
  static constexpr size_t kMaxComponents = 16;
  using Id = int8_t;
  static constexpr Id kInvalid = -1;

  using UptimeSFn = uint32_t (*)();
  explicit HealthRegistry(UptimeSFn uptimeS) : uptimeS_(uptimeS) {}

  // Registers (or finds) a component by name. Returns kInvalid if the registry is full.
  Id add(const char* name, ComponentStatus initial = ComponentStatus::Unknown);

  void setStatus(Id id, ComponentStatus status);
  // Records an error and sets the status (default Error). `message` is copied (truncated).
  void reportError(Id id, int32_t code, const char* message, ComponentStatus status = ComponentStatus::Error);
  // Records a success and sets the status to Ok.
  void reportSuccess(Id id, uint32_t utc = 0);

  bool get(Id id, ComponentHealth& out) const;
  size_t count() const { return count_; }
  // Worst status over all components (Disabled and Unknown are ignored).
  ComponentStatus overall() const;

 private:
  bool valid(Id id) const { return id >= 0 && static_cast<size_t>(id) < count_; }

  UptimeSFn uptimeS_;
  ComponentHealth items_[kMaxComponents] = {};
  size_t count_ = 0;
  mutable Mutex mutex_;
};
