#pragma once

#include <stdint.h>

// SD overflow protection (plan §4.5.1): decides which file to delete next. Pure logic; SdStore
// collects the facts (free space, oldest files) and carries out the decision.
//
// Rules:
//   - Logs may use at most maxLogFiles files (10 % of the card, max. 100 MB); beyond that the
//     oldest log file is deleted, whatever the free space.
//   - When free space drops below lowWatermarkPct (default 10 %, i.e. the card is 90 % full),
//     files are deleted, oldest first and logs before measurements, until free space is back at
//     highWatermarkPct (default 20 %).
//   - The low watermark is a hard limit: below it, the oldest file is deleted whatever its age.
//     minRetentionDays only protects measurement files from the further cleanup between the two
//     watermarks.
//   - The files currently being written are never candidates (SdStore excludes them). If nothing
//     else is left while below the low watermark, the result is Full.

namespace janitor {

struct Limits {
  uint8_t lowWatermarkPct = 10;
  uint8_t highWatermarkPct = 20;
  uint16_t minRetentionDays = 30;
  uint32_t maxLogFiles = 100;
};

struct Facts {
  uint64_t totalBytes = 0;
  uint64_t freeBytes = 0;
  uint32_t logFiles = 0;           // all log files, including the current one
  bool haveOldestLog = false;      // a deletable (not current) log file exists
  bool haveOldestData = false;     // a deletable measurement file exists
  uint32_t oldestDataMtime = 0;    // UTC, 0 = unknown
  uint32_t nowUtc = 0;             // 0 = clock not synced
};

enum class Action : uint8_t { None, DeleteOldestLog, DeleteOldestData, Full };

// `cleaning` keeps the state between calls (hysteresis between the two watermarks).
Action decide(const Facts& facts, const Limits& limits, bool& cleaning);

// Max. number of log files for a card of `totalBytes` with files of `logFileBytes`.
uint32_t maxLogFilesFor(uint64_t totalBytes, uint32_t logFileBytes);

}  // namespace janitor
