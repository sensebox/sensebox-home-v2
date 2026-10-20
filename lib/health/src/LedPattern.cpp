#include "LedPattern.h"

#include <string.h>

namespace ledpattern {

namespace {
bool startsWith(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }
}  // namespace

LedSignals fromHealth(const HealthRegistry& health) {
  LedSignals out;
  for (size_t i = 0; i < health.count(); ++i) {
    ComponentHealth h;
    if (!health.get(static_cast<HealthRegistry::Id>(i), h)) continue;

    if (strcmp(h.name, "config") == 0) {
      // "no configuration stored" is Degraded: the box still runs (unconfigured).
      if (h.status == ComponentStatus::Error) out.critical = true;
    } else if (strcmp(h.name, "lorawan") == 0) {
      // Degraded without any error yet = still joining; with errors = join failed on all
      // spreading factors (EV_JOIN_FAILED), or the link was lost.
      // Unknown/Disabled = transport not started (no data target configured): no blink.
      if (h.status == ComponentStatus::Error || (h.status == ComponentStatus::Degraded && h.errorCount > 0)) {
        out.connectFailed = true;
      } else if (h.status == ComponentStatus::Degraded) {
        out.settingUp = true;
      }
    } else if (startsWith(h.name, "sensor")) {
      // A sensor stays Unknown until its first reading.
      if (h.status == ComponentStatus::Error) out.sensorFailed = true;
      if (h.status == ComponentStatus::Unknown) out.settingUp = true;
    } else if (strcmp(h.name, "sd") == 0) {
      if (h.status == ComponentStatus::Error) out.storageFailed = true;
    }
    // "uplink:*" is left out: its problems are covered by the transport and the sensors.
  }
  return out;
}

LedColor colorAt(const LedSignals& signals, uint32_t nowMs) {
  if (signals.critical) return LedColor::Red;

  LedColor blinks[4];
  uint8_t n = 0;
  if (signals.settingUp) blinks[n++] = LedColor::White;
  if (signals.connectFailed) blinks[n++] = LedColor::Yellow;
  if (signals.sensorFailed) blinks[n++] = LedColor::Red;
  if (signals.storageFailed) blinks[n++] = LedColor::Purple;

  const uint32_t t = nowMs % kPeriodMs;
  const uint32_t slot = t / (kBlinkMs + kGapMs);
  if (slot < n && t % (kBlinkMs + kGapMs) < kBlinkMs) return blinks[slot];
  return LedColor::Green;
}

}  // namespace ledpattern
