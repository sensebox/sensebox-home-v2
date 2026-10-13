#include "Clock.h"

#include "Measurement.h"

bool Clock::setUtc(uint64_t utcMs, uint64_t atUptimeMs, TimeSource source) {
  if (source == TimeSource::None) return false;
  if (utcMs / 1000 < kMinValidUtc) return false;
  if (source == TimeSource::Browser && source_ != TimeSource::None && source_ != TimeSource::Browser) {
    return false;
  }
  utcAtBootMs_ = static_cast<int64_t>(utcMs) - static_cast<int64_t>(atUptimeMs);
  source_ = source;
  lastSyncUptimeMs_ = uptimeMs_();
  return true;
}

uint32_t Clock::nowUtc() const {
  if (!isSynced()) return 0;
  return static_cast<uint32_t>((utcAtBootMs_ + static_cast<int64_t>(uptimeMs_())) / 1000);
}

uint32_t Clock::timestampNow(uint16_t& quality) const {
  if (isSynced()) return nowUtc();
  quality |= quality::kTimeUnsynced;
  return uptimeS();
}

bool Clock::toUtc(uint32_t uptimeSeconds, uint32_t& utcOut) const {
  if (!isSynced()) return false;
  utcOut = static_cast<uint32_t>((utcAtBootMs_ + static_cast<int64_t>(uptimeSeconds) * 1000) / 1000);
  return true;
}
