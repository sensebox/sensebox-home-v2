#include "Janitor.h"

namespace janitor {

Action decide(const Facts& f, const Limits& limits, bool& cleaning) {
  if (f.totalBytes == 0) return Action::None;

  const bool tooManyLogs = f.logFiles > limits.maxLogFiles;
  const bool logsTooBig = static_cast<uint64_t>(f.rotatedLogFiles) * limits.logFileBytes > limits.maxLogBytes;
  if ((tooManyLogs || logsTooBig) && f.haveOldestLog) return Action::DeleteOldestLog;

  const uint64_t low = f.totalBytes / 100 * limits.lowWatermarkPct;
  const uint64_t high = f.totalBytes / 100 * limits.highWatermarkPct;
  if (f.freeBytes < low) cleaning = true;
  if (cleaning && f.freeBytes >= high) cleaning = false;
  if (!cleaning) return Action::None;

  const bool overLimit = f.freeBytes < low;
  if (f.haveOldestLog) return Action::DeleteOldestLog;
  if (f.haveOldestData) {
    const bool known = f.oldestDataMtime != 0 && f.nowUtc != 0 && f.nowUtc >= f.oldestDataMtime;
    const bool young =
        known && f.nowUtc - f.oldestDataMtime < static_cast<uint32_t>(limits.minRetentionDays) * 86400u;
    if (!young || overLimit) return Action::DeleteOldestData;
  }
  // Nothing (more) may be deleted. Between the watermarks that's fine (retention wins);
  // above the limit only the current files are left.
  return overLimit ? Action::Full : Action::None;
}

uint64_t maxLogBytesFor(uint64_t totalBytes, uint32_t logFileBytes) {
  uint64_t budget = totalBytes / 10;
  const uint64_t cap = 100ull * 1024 * 1024;
  if (budget > cap) budget = cap;
  const uint64_t min = 4ull * logFileBytes;
  return budget < min ? min : budget;
}

}  // namespace janitor
