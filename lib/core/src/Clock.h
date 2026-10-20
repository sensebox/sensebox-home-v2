#pragma once

#include <stdint.h>

// UTC wall clock on top of a monotonic 64-bit uptime counter. See IMPLEMENTATION_PLAN.md §6.1.
//
// Until the first sync, timestamps are uptime seconds flagged with quality::kTimeUnsynced.
// They can be converted to UTC later during the same boot with toUtc().

enum class TimeSource : uint8_t {
  None = 0,
  Browser,     // service-mode UI; only accepted while no better source has synced
  LoRaWan,     // DeviceTimeReq / DeviceTimeAns
  Ntp,
};

class Clock {
 public:
  using UptimeMsFn = uint64_t (*)();

  // Smallest plausible UTC time (2025-01-01). Anything below is rejected as invalid.
  static constexpr uint32_t kMinValidUtc = 1735689600u;
  // GPS epoch (1980-01-06) in Unix seconds, and GPS-UTC leap seconds (valid since 2017-01-01).
  static constexpr uint32_t kGpsEpochUnix = 315964800u;
  static constexpr uint32_t kGpsLeapSeconds = 18u;

  explicit Clock(UptimeMsFn uptimeMs) : uptimeMs_(uptimeMs) {}

  uint64_t uptimeMs() const { return uptimeMs_(); }
  uint32_t uptimeS() const { return static_cast<uint32_t>(uptimeMs_() / 1000); }

  bool isSynced() const { return source_ != TimeSource::None; }
  TimeSource source() const { return source_; }

  // Sets the clock: `utcMs` was the UTC time (ms) at uptime `atUptimeMs`.
  // Returns false if the time is implausible or a browser time arrives after a real sync.
  bool setUtc(uint64_t utcMs, uint64_t atUptimeMs, TimeSource source);

  // Current UTC seconds, or 0 if not synced.
  uint32_t nowUtc() const;
  // Current UTC milliseconds, or 0 if not synced.
  uint64_t nowUtcMs() const;
  // Counts successful setUtc() calls, so schedules on the UTC grid notice a (re)sync.
  uint32_t syncCount() const { return syncCount_; }

  // Timestamp for a new measurement: UTC if synced, otherwise uptime seconds.
  // ORs quality::kTimeUnsynced into `quality` when not synced.
  uint32_t timestampNow(uint16_t& quality) const;

  // Converts an unsynced uptime-seconds timestamp of this boot to UTC. False if not synced.
  bool toUtc(uint32_t uptimeSeconds, uint32_t& utcOut) const;

  // Uptime (ms) of the last successful sync; 0 if never synced.
  uint64_t lastSyncUptimeMs() const { return lastSyncUptimeMs_; }

  static uint64_t gpsToUnixMs(uint64_t gpsMs) {
    return gpsMs + static_cast<uint64_t>(kGpsEpochUnix - kGpsLeapSeconds) * 1000u;
  }

 private:
  UptimeMsFn uptimeMs_;
  TimeSource source_ = TimeSource::None;
  // UTC (ms) corresponding to uptime 0. Valid when synced.
  int64_t utcAtBootMs_ = 0;
  uint64_t lastSyncUptimeMs_ = 0;
  uint32_t syncCount_ = 0;
};
