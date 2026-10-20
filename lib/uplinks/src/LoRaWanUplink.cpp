#include "LoRaWanUplink.h"

#include <stdio.h>
#include <string.h>

#include "Log.h"
#include "Scheduling.h"

namespace {
constexpr const char* kTag = "uplink";
}

void LoRaWanUplink::WindowCollector::onMeasurement(const Measurement& m) {
  if (count < kMax) items[count++] = m;
  if (forward != nullptr) forward->onMeasurement(m);
}

LoRaWanUplink::LoRaWanUplink(const config::StationConfig& station, config::UplinkType type,
                             const config::LoRaWanConfig& cfg, Clock& clock, HealthRegistry& health,
                             LoRaWanTransport& transport)
    : station_(station),
      type_(type),
      cfg_(cfg),
      clock_(clock),
      health_(health),
      transport_(transport),
      healthId_(health.add("uplink:lorawan")) {
  // Per-box send delay: FNV-1a of the DevEUI, spread over [0, min(kMaxSendDelayMs, interval / 4)).
  uint32_t hash = 2166136261u;
  for (const char* c = cfg_.devEui; *c != '\0'; ++c) hash = (hash ^ static_cast<uint8_t>(*c)) * 16777619u;
  uint32_t maxDelayMs = cfg_.intervalSec * 250u;
  if (maxDelayMs > kMaxSendDelayMs) maxDelayMs = kMaxSendDelayMs;
  sendDelayMs_ = maxDelayMs > 0 ? hash % maxDelayMs : 0;
}

void LoRaWanUplink::begin(uint32_t nowMs) {
  const uint32_t windowSec = cfg_.intervalSec;
  for (uint8_t i = 0; i < station_.sensorCount; ++i) {
    const config::SensorConfig& s = station_.sensors[i];
    const uint32_t expected = s.intervalSec > 0 ? windowSec / s.intervalSec : 0;
    aggregator_.setExpectedSamples(s.slot, static_cast<uint16_t>(expected > 65535 ? 65535 : expected));
  }
  windowEndMs_ = nowMs + windowSec * 1000u;
  windowEndUtc_ = 0;
  gridSync_ = 0;
  pendingLen_ = 0;
  started_ = true;
  if (onGrid()) alignWindow(nowMs, true);
  LOGI(kTag, "%s: window %lu s, %u channels, send delay %lu s", config::uplinkTypeName(type_),
       static_cast<unsigned long>(windowSec), cfg_.channelCount, static_cast<unsigned long>(sendDelayMs_ / 1000));
}

void LoRaWanUplink::onMeasurement(const Measurement& m) {
  if (m.kind == ValueKind::Aggregated) return;
  aggregator_.add(m);
}

bool LoRaWanUplink::timeSyncDue() const {
  if (!clock_.isSynced() || clock_.source() == TimeSource::Browser) return true;
  const uint64_t sinceSyncMs = clock_.uptimeMs() - clock_.lastSyncUptimeMs();
  return sinceSyncMs >= static_cast<uint64_t>(cfg_.timeSyncIntervalSec) * 1000u;
}

bool LoRaWanUplink::onGrid() const { return clock_.isSynced() && alignsToDay(cfg_.intervalSec); }

// Lets the current window end on the next interval boundary of the UTC clock. With `mergeShort`
// (first alignment after boot) a window that would end in less than half an interval is merged
// into the next one, so it isn't built from a handful of samples.
void LoRaWanUplink::alignWindow(uint32_t nowMs, bool mergeShort) {
  const uint32_t periodMs = cfg_.intervalSec * 1000u;
  const uint64_t utcMs = clock_.nowUtcMs();
  uint32_t untilEndMs = msToNextMultiple(utcMs, periodMs);
  if (mergeShort && untilEndMs < periodMs / 2) untilEndMs += periodMs;
  windowEndUtc_ = static_cast<uint32_t>((utcMs + untilEndMs) / 1000);
  windowEndMs_ = nowMs + untilEndMs + kCloseDelayMs;
  gridSync_ = clock_.syncCount();
}

void LoRaWanUplink::tick(uint32_t nowMs) {
  if (!started_) return;
  // First sync: move the open window onto the grid. A resync only corrects it by a few seconds.
  if (onGrid() && gridSync_ != clock_.syncCount()) alignWindow(nowMs, windowEndUtc_ == 0);
  if (timeReached(nowMs, windowEndMs_)) {
    closeWindow(nowMs);
    if (windowEndUtc_ != 0) {
      alignWindow(nowMs, false);
    } else {
      windowEndMs_ += cfg_.intervalSec * 1000u;
      // After a long stall don't fire several windows back to back.
      if (timeReached(nowMs, windowEndMs_)) windowEndMs_ = nowMs + cfg_.intervalSec * 1000u;
    }
  }
  if (pendingLen_ > 0 && timeReached(nowMs, sendAtMs_)) sendPending();
}

void LoRaWanUplink::closeWindow(uint32_t nowMs) {
  ++stats_.windows;
  uint16_t tsQuality = 0;
  const uint32_t ts = windowEndUtc_ != 0 ? windowEndUtc_ : clock_.timestampNow(tsQuality);
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
  const size_t length = encodeFrame(ts, (tsQuality & quality::kTimeUnsynced) == 0, frame, sizeof(frame));
  stats_.lastLength = static_cast<uint8_t>(length);
  memcpy(stats_.lastFrame, frame, length);
  char hex[2 * cayenne::kMaxPayload + 1] = {};
  for (size_t i = 0; i < length; ++i) snprintf(hex + 2 * i, 3, "%02X", frame[i]);
  LOGD(kTag, "frame (%u B): %s", static_cast<unsigned>(length), hex);
  if (length == 0) {
    ++stats_.skipped;
    return;
  }
  if (pendingLen_ > 0) {
    ++stats_.skipped;
    LOGW(kTag, "previous window not sent yet, replaced");
  }
  memcpy(pending_, frame, length);
  pendingLen_ = static_cast<uint8_t>(length);
  sendAtMs_ = nowMs + sendDelayMs_;
}

// Encodes the window means. Returns the frame length, 0 if there is nothing to send.
size_t LoRaWanUplink::encodeFrame(uint32_t ts, bool utc, uint8_t* out, size_t cap) {
  uint8_t clamped = 0;
  uint8_t missing = 0;
  uint8_t encoded = 0;
  size_t length = 0;
  if (type_ == config::UplinkType::LoRaWanCompact) {
    // Before the first sync the time field is 0 and TTN's receive time is all there is.
    const compact::EncodeResult r = compact::encode(cfg_, utc ? ts : 0, window_.items, window_.count, out, cap);
    clamped = r.clamped;
    missing = r.missing;
    encoded = r.encoded;
    length = r.length;
  } else {
    const cayenne::EncodeResult r = cayenne::encode(cfg_, window_.items, window_.count, out, cap);
    clamped = r.clamped;
    missing = r.missing;
    encoded = r.encoded;
    length = r.length;
  }
  stats_.clampedValues += clamped;
  if (clamped > 0) LOGW(kTag, "%u value(s) clamped to the encoding's range", clamped);
  if (missing > 0) LOGI(kTag, "%u channel(s) without value", missing);
  return encoded > 0 ? length : 0;
}

void LoRaWanUplink::sendPending() {
  const uint8_t len = pendingLen_;
  pendingLen_ = 0;
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
  if (!transport_.send(port(), pending_, len, requestTime)) {
    ++stats_.skipped;
    health_.reportError(healthId_, 4, "send rejected", ComponentStatus::Degraded);
    LOGW(kTag, "transport rejected the frame");
    return;
  }
  ++stats_.sent;
  health_.reportSuccess(healthId_, clock_.nowUtc());
  LOGI(kTag, "queued %u bytes%s", static_cast<unsigned>(len), requestTime ? ", time sync requested" : "");
}
