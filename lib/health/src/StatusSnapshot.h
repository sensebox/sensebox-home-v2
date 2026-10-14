#pragma once

#include <stddef.h>
#include <stdint.h>

// Status of normal operation, frozen when the user requests service mode (plan §4.8).
//
// The box restarts into service mode without running sensors/LoRaWAN (RAM is short: no PSRAM),
// so the service web UI shows this snapshot instead of live values. It is kept in RTC memory,
// which survives the software restart; therefore it holds only plain fixed-size data
// (names are stored as strings, so reading it needs no knowledge of the modules).

struct SnapshotComponent {
  char name[24];
  char status[10];
  uint32_t errorCount;
  char lastError[48];
  uint32_t lastErrorUptimeS;
  uint32_t lastSuccessUptimeS;  // 0 = never
};

struct SnapshotSensor {
  uint8_t slot;
  char type[16];
  char state[14];
  char lastError[14];
  uint32_t errorCount;
  uint32_t readingCount;
  uint32_t lastGoodUptimeS;  // 0 = never
  uint32_t retryInS;
};

struct SnapshotValue {
  uint8_t slot;
  char phenomenon[18];
  char unit[8];
  float value;
  uint16_t quality;
  uint32_t timestamp;  // UTC, or uptime seconds if the quality has the "time unsynced" flag
};

struct SnapshotLoRaWan {
  bool configured;
  bool appKeySet;
  bool joined;
  uint8_t spreadingFactor;
  int16_t lastRssi;
  int8_t lastSnr;
  uint32_t joinAttempts;
  uint32_t uplinks;
  uint32_t downlinks;
  int32_t budgetMs;
};

struct SnapshotLogLine {
  uint32_t uptimeS;
  char level;
  char tag[12];
  char msg[84];
};

struct StatusSnapshot {
  static constexpr uint32_t kMagic = 0x53425353;  // "SBSS"
  static constexpr uint16_t kVersion = 1;
  static constexpr size_t kMaxComponents = 10;
  static constexpr size_t kMaxSensors = 8;
  static constexpr size_t kMaxValues = 16;
  static constexpr size_t kMaxLogLines = 12;

  // header, see seal()/isValid()
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t crc;

  char firmware[16];
  char stationName[32];
  char resetReason[20];
  char previousBreadcrumb[24];
  uint32_t uptimeS;
  uint32_t utc;  // 0 if the clock was not synced
  bool safeMode;
  uint32_t freeHeap;
  uint32_t minFreeHeap;
  uint32_t largestFreeBlock;
  char overall[10];

  uint8_t componentCount;
  SnapshotComponent components[kMaxComponents];
  uint8_t sensorCount;
  SnapshotSensor sensors[kMaxSensors];
  uint8_t valueCount;
  SnapshotValue values[kMaxValues];
  SnapshotLoRaWan lorawan;
  uint8_t logCount;
  SnapshotLogLine log[kMaxLogLines];  // oldest first
};

namespace snapshot {

// Zeroes everything (call before filling).
void clear(StatusSnapshot& s);
// Sets magic/version/size and the checksum. Call after filling.
void seal(StatusSnapshot& s);
// True if `s` was sealed by this firmware version and is unchanged since.
bool isValid(const StatusSnapshot& s);
void invalidate(StatusSnapshot& s);

// Copies `src` into `dst` (truncating, always terminated).
void copyText(char* dst, size_t cap, const char* src);

// JSON for the service web UI. `withLog` adds the log lines. Returns the length, 0 if `cap`
// is too small (then `buf` is an empty string).
size_t toJson(const StatusSnapshot& s, char* buf, size_t cap, bool withLog);

uint32_t crc32(const void* data, size_t len);

}  // namespace snapshot
