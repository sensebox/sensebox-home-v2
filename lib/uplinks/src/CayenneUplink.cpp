#include "CayenneUplink.h"

#include <stdio.h>
#include <string.h>

#include "Log.h"
#include "Scheduling.h"

namespace {
constexpr const char* kTag = "cayenne";
}

void CayenneUplink::WindowCollector::onMeasurement(const Measurement& m) {
  if (count < kMax) items[count++] = m;
  if (forward != nullptr) forward->onMeasurement(m);
}

CayenneUplink::CayenneUplink(const config::StationConfig& station, const config::LoRaWanConfig& cfg, Clock& clock,
                             HealthRegistry& health, LoRaWanTransport& transport)
    : station_(station),
      cfg_(cfg),
      clock_(clock),
      health_(health),
      transport_(transport),
      healthId_(health.add("uplink:lorawan")) {}

void CayenneUplink::begin(uint32_t nowMs) {
  const uint32_t windowSec = cfg_.intervalSec;
  for (uint8_t i = 0; i < station_.sensorCount; ++i) {
    const config::SensorConfig& s = station_.sensors[i];
    const uint32_t expected = s.intervalSec > 0 ? windowSec / s.intervalSec : 0;
    aggregator_.setExpectedSamples(s.slot, static_cast<uint16_t>(expected > 65535 ? 65535 : expected));
  }
  windowEndMs_ = nowMs + windowSec * 1000u;
  started_ = true;
  LOGI(kTag, "window %lu s, %u channels", static_cast<unsigned long>(windowSec), cfg_.channelCount);
}

void CayenneUplink::onMeasurement(const Measurement& m) {
  if (m.kind == ValueKind::Aggregated) return;
  aggregator_.add(m);
}

bool CayenneUplink::timeSyncDue() const {
  if (!clock_.isSynced() || clock_.source() == TimeSource::Browser) return true;
  const uint64_t sinceSyncMs = clock_.uptimeMs() - clock_.lastSyncUptimeMs();
  return sinceSyncMs >= static_cast<uint64_t>(cfg_.timeSyncIntervalSec) * 1000u;
}

void CayenneUplink::tick(uint32_t nowMs) {
  if (!started_ || !timeReached(nowMs, windowEndMs_)) return;
  closeWindow(nowMs);
  windowEndMs_ += cfg_.intervalSec * 1000u;
  // After a long stall don't fire several windows back to back.
  if (timeReached(nowMs, windowEndMs_)) windowEndMs_ = nowMs + cfg_.intervalSec * 1000u;
}

void CayenneUplink::closeWindow(uint32_t nowMs) {
  (void)nowMs;
  ++stats_.windows;
  uint16_t tsQuality = 0;
  const uint32_t ts = clock_.timestampNow(tsQuality);
  window_.reset();
  window_.forward = aggregateSink_;
  aggregator_.flush(ts, tsQuality, window_);

  if (window_.count == 0) {
    ++stats_.skipped;
    health_.reportError(healthId_, 1, "no valid values in window", ComponentStatus::Degraded);
    LOGW(kTag, "window %lu: no valid values, nothing to send", static_cast<unsigned long>(stats_.windows));
    return;
  }

  uint8_t frame[cayenne::kMaxPayload];
  const cayenne::EncodeResult r = cayenne::encode(cfg_, window_.items, window_.count, frame, sizeof(frame));
  stats_.clampedValues += r.clamped;
  stats_.lastLength = static_cast<uint8_t>(r.length);
  memcpy(stats_.lastFrame, frame, r.length);
  char hex[2 * cayenne::kMaxPayload + 1] = {};
  for (size_t i = 0; i < r.length; ++i) snprintf(hex + 2 * i, 3, "%02X", frame[i]);
  LOGD(kTag, "frame (%u B): %s", static_cast<unsigned>(r.length), hex);
  if (r.clamped > 0) LOGW(kTag, "%u value(s) clamped to the Cayenne range", r.clamped);
  if (r.missing > 0) LOGI(kTag, "%u channel(s) without value", r.missing);

  if (r.length == 0) {
    ++stats_.skipped;
    return;
  }
  if (!transport_.joined()) {
    ++stats_.skipped;
    health_.reportError(healthId_, 2, "not joined, window dropped (kept on SD)", ComponentStatus::Degraded);
    LOGW(kTag, "not joined, window dropped");
    return;
  }
  if (transport_.busy()) {
    ++stats_.skipped;
    health_.reportError(healthId_, 3, "transport busy, window dropped", ComponentStatus::Degraded);
    LOGW(kTag, "transport busy, window dropped");
    return;
  }
  const bool requestTime = timeSyncDue();
  if (!transport_.send(kPort, frame, r.length, requestTime)) {
    ++stats_.skipped;
    health_.reportError(healthId_, 4, "send rejected", ComponentStatus::Degraded);
    LOGW(kTag, "transport rejected the frame");
    return;
  }
  ++stats_.sent;
  health_.reportSuccess(healthId_, clock_.nowUtc());
  LOGI(kTag, "queued %u bytes, %u channels%s", static_cast<unsigned>(r.length), r.encoded,
       requestTime ? ", time sync requested" : "");
}
