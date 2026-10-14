#include "StatusCapture.h"

#include "Log.h"
#include "PersistentStore.h"
#include "SystemInfo.h"
#include "Version.h"

using snapshot::copyText;

void captureStatus(const App& app, const LatestValues& latest, uint32_t nowMs, StatusSnapshot& out) {
  snapshot::clear(out);
  copyText(out.firmware, sizeof(out.firmware), SBH_FW_VERSION);
  copyText(out.stationName, sizeof(out.stationName), app.config.name);
  copyText(out.resetReason, sizeof(out.resetReason), sysinfo::resetReason());
  copyText(out.previousBreadcrumb, sizeof(out.previousBreadcrumb), sysinfo::previousBreadcrumb());
  out.uptimeS = app.clock.uptimeS();
  out.utc = app.clock.nowUtc();
  out.safeMode = app.safeMode;
  out.freeHeap = sysinfo::freeHeap();
  out.minFreeHeap = sysinfo::minFreeHeap();
  out.largestFreeBlock = sysinfo::largestFreeBlock();
  copyText(out.overall, sizeof(out.overall), componentStatusName(app.health.overall()));

  for (size_t i = 0; i < app.health.count() && out.componentCount < StatusSnapshot::kMaxComponents; ++i) {
    ComponentHealth h;
    if (!app.health.get(static_cast<HealthRegistry::Id>(i), h)) continue;
    SnapshotComponent& c = out.components[out.componentCount++];
    copyText(c.name, sizeof(c.name), h.name);
    copyText(c.status, sizeof(c.status), componentStatusName(h.status));
    c.errorCount = h.errorCount;
    copyText(c.lastError, sizeof(c.lastError), h.lastError);
    c.lastErrorUptimeS = h.lastErrorUptimeS;
    c.lastSuccessUptimeS = h.lastSuccessUptimeS;
  }

  for (size_t i = 0; i < app.sensors.slotCount() && out.sensorCount < StatusSnapshot::kMaxSensors; ++i) {
    SensorSlotStatus st;
    if (!app.sensors.status(i, nowMs, st)) continue;
    SnapshotSensor& s = out.sensors[out.sensorCount++];
    s.slot = st.slot;
    copyText(s.type, sizeof(s.type), st.type);
    copyText(s.state, sizeof(s.state), sensorStateName(st.state));
    copyText(s.lastError, sizeof(s.lastError), sensorErrorName(st.lastError));
    s.errorCount = st.errorCount;
    s.readingCount = st.readingCount;
    s.lastGoodUptimeS = st.lastGoodUptimeS;
    s.retryInS = st.retryInMs / 1000;
  }

  for (size_t i = 0; i < latest.count() && out.valueCount < StatusSnapshot::kMaxValues; ++i) {
    const Measurement& m = latest.at(i);
    SnapshotValue& v = out.values[out.valueCount++];
    v.slot = m.sensorSlot;
    copyText(v.phenomenon, sizeof(v.phenomenon), phenomenonName(m.phenomenon));
    copyText(v.unit, sizeof(v.unit), unitSymbol(m.unit));
    v.value = m.value;
    v.quality = m.quality;
    v.timestamp = m.timestamp;
  }

  for (uint8_t i = 0; i < app.config.uplinkCount; ++i) {
    if (app.config.uplinks[i].type == config::UplinkType::LoRaWanCayenne) out.lorawan.configured = true;
  }
  const LmicTransport::Status ls = app.lora.status(nowMs);
  out.lorawan.appKeySet = persist::hasSecret(persist::secret::kLoRaAppKey);
  out.lorawan.joined = ls.joined;
  out.lorawan.spreadingFactor = ls.spreadingFactor;
  out.lorawan.lastRssi = ls.lastRssi;
  out.lorawan.lastSnr = ls.lastSnr;
  out.lorawan.joinAttempts = ls.joinAttempts;
  out.lorawan.uplinks = ls.uplinks;
  out.lorawan.downlinks = ls.downlinks;
  out.lorawan.budgetMs = ls.budgetMs;

  // Newest log lines, oldest first.
  const size_t lines = logger::lineCount();
  const size_t n = lines < StatusSnapshot::kMaxLogLines ? lines : StatusSnapshot::kMaxLogLines;
  for (size_t age = n; age > 0; --age) {
    LogLine line;
    if (!logger::lineAt(age - 1, line)) continue;
    SnapshotLogLine& l = out.log[out.logCount++];
    l.uptimeS = line.uptimeS;
    l.level = logger::levelLetter(line.level);
    copyText(l.tag, sizeof(l.tag), line.tag);
    copyText(l.msg, sizeof(l.msg), line.msg);
  }

  snapshot::seal(out);
}
