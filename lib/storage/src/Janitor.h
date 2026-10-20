#pragma once

#include <stdint.h>

// SD overflow protection (plan §4.5.1): decides which file to delete next. Pure logic; SdStore
// collects the facts (free space, oldest files) and carries out the decision.
//
// Rules:
//   - Logs are capped twice; beyond either cap the oldest log file is deleted, whatever the free
//     space:
//       - bytes: rotated (hence full) log files may use at most maxLogBytes (10 % of the card,
//         max. 100 MB). The listing has names only, no sizes, so the last part of each boot
//         (usually small) isn't counted here;
//       - files: at most maxLogFiles (1000) files. This keeps the history of a box stuck in a
//         reboot loop, where every boot leaves one tiny log file, and keeps the directory small
//         enough to be listed quickly at every boot.
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
  uint32_t maxLogFiles = 1000;
  uint64_t maxLogBytes = 100ull * 1024 * 1024;
  uint32_t logFileBytes = 1024u * 1024u;  // size at which a log file is rotated
};

struct Facts {
  uint64_t totalBytes = 0;
  uint64_t freeBytes = 0;
  uint32_t logFiles = 0;           // all log files, including the current one
  uint32_t rotatedLogFiles = 0;    // log files followed by a newer part of the same boot (full)
  bool haveOldestLog = false;      // a deletable (not current) log file exists
  bool haveOldestData = false;     // a deletable measurement file exists
  uint32_t oldestDataMtime = 0;    // UTC, 0 = unknown
  uint32_t nowUtc = 0;             // 0 = clock not synced
};

enum class Action : uint8_t { None, DeleteOldestLog, DeleteOldestData, Full };

// `cleaning` keeps the state between calls (hysteresis between the two watermarks).
Action decide(const Facts& facts, const Limits& limits, bool& cleaning);

// Byte budget of the logs on a card of `totalBytes` with files of `logFileBytes`.
uint64_t maxLogBytesFor(uint64_t totalBytes, uint32_t logFileBytes);

}  // namespace janitor
