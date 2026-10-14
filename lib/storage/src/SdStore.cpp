#include "SdStore.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include "Scheduling.h"

namespace {

constexpr const char* kTag = "sd";
constexpr uint32_t kLockTimeoutMs = 20;
constexpr uint16_t kMaxPart = 9999;

const char* baseName(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash != nullptr ? slash + 1 : path;
}

// Collects, in one pass over a directory, the number of our files and the oldest one that is
// not `current`.
struct ScanCtx {
  const char* current;
  uint32_t count = 0;
  bool have = false;
  uint64_t minKey = 0;
  char minName[24] = {};  // "B000042-0001.csv"
};

void onFile(const char* name, void* p) {
  ScanCtx& ctx = *static_cast<ScanCtx*>(p);
  uint32_t boot;
  uint16_t part;
  if (!storagefmt::parseFileName(name, boot, part)) return;  // not ours: never touched
  ++ctx.count;
  if (strcmp(name, ctx.current) == 0) return;
  const uint64_t key = storagefmt::fileKey(boot, part);
  if (ctx.have && key >= ctx.minKey) return;
  ctx.have = true;
  ctx.minKey = key;
  strncpy(ctx.minName, name, sizeof(ctx.minName) - 1);
  ctx.minName[sizeof(ctx.minName) - 1] = '\0';
}

}  // namespace

SdStore::SdStore(FileSystem& fs, Clock& clock, HealthRegistry& health)
    : fs_(fs), clock_(clock), health_(health), healthId_(health.add("sd")) {}

SdStore::~SdStore() {
  delete[] dataBuf_;
  delete[] logBuf_[0];
  delete[] logBuf_[1];
}

bool SdStore::begin(const Options& options, uint32_t nowMs) {
  opt_ = options;
  if (opt_.storage.sd == config::SdMode::Off) {
    state_ = State::Off;
    health_.reportError(healthId_, 0, "off in config", ComponentStatus::Disabled);
    LOGI(kTag, "SD card disabled in config");
    return false;
  }
  dataBuf_ = new (std::nothrow) char[opt_.dataBufferBytes];
  logBuf_[0] = new (std::nothrow) char[opt_.logBufferBytes];
  logBuf_[1] = new (std::nothrow) char[opt_.logBufferBytes];
  if (dataBuf_ == nullptr || logBuf_[0] == nullptr || logBuf_[1] == nullptr) {
    state_ = State::Off;
    health_.reportError(healthId_, 5, "out of memory");
    LOGE(kTag, "out of memory, SD card not used");
    return false;
  }
  state_ = State::Unmounted;
  tryMount(nowMs);
  return true;
}

void SdStore::tryMount(uint32_t nowMs) {
  if (!fs_.mount()) {
    const uint32_t delay = backoffMs(opt_.retryBaseMs, opt_.retryMaxMs, retryAttempt_);
    if (retryAttempt_ < 255) ++retryAttempt_;
    retryAtMs_ = nowMs + delay;
    if (everMounted_) {
      char msg[ComponentHealth::kErrorLen];
      snprintf(msg, sizeof(msg), "card removed or failing, retry in %lu s", static_cast<unsigned long>(delay / 1000));
      health_.reportError(healthId_, 2, msg);
      LOGW(kTag, "%s", msg);
    } else if (retryAttempt_ == 1) {
      // Not an error: the card is optional ("sd": "auto").
      health_.reportError(healthId_, 1, "no SD card", ComponentStatus::Disabled);
      LOGI(kTag, "no SD card: measuring without history (checking again from time to time)");
    }
    return;
  }

  if (!fs_.ensureDir(storagefmt::kDataDir) || !fs_.ensureDir(storagefmt::kLogDir)) {
    fail("creating directories", nowMs);
    return;
  }
  if (dataPart_ == 0) {
    // First mount in this boot: start new files (skipping names that exist, e.g. same boot ID).
    dataPart_ = 1;
    do {
      storagefmt::filePath(storagefmt::kDataDir, opt_.bootId, dataPart_, "csv", dataPath_, sizeof(dataPath_));
    } while (fs_.fileSize(dataPath_) >= 0 && ++dataPart_ < kMaxPart);
    logPart_ = 1;
    do {
      storagefmt::filePath(storagefmt::kLogDir, opt_.bootId, logPart_, "log", logPath_, sizeof(logPath_));
    } while (fs_.fileSize(logPath_) >= 0 && ++logPart_ < kMaxPart);
  }
  // On a remount (possibly another card) continue the current files, or start them anew.
  dataSize_ = fs_.fileSize(dataPath_);
  logSize_ = fs_.fileSize(logPath_);

  const bool first = !everMounted_;
  everMounted_ = true;
  state_ = State::Mounted;
  retryAttempt_ = 0;
  lastFlushMs_ = nowMs;
  janitorAtMs_ = nowMs;  // check the free space right away
  health_.reportSuccess(healthId_, clock_.nowUtc());
  if (first) copyLogRing();  // the boot log so far
  LOGI(kTag, "card mounted: %s, %s", dataPath_, logPath_);
}

void SdStore::fail(const char* what, uint32_t nowMs) {
  ++writeErrors_;
  fs_.unmount();
  state_ = State::Unmounted;
  // First retry soon (a single glitch), then with backoff.
  const uint32_t delay = retryAttempt_ == 0 ? 2000 : backoffMs(opt_.retryBaseMs, opt_.retryMaxMs, retryAttempt_ - 1);
  if (retryAttempt_ < 255) ++retryAttempt_;
  retryAtMs_ = nowMs + delay;
  char msg[ComponentHealth::kErrorLen];
  snprintf(msg, sizeof(msg), "error %s, retry in %lu s", what, static_cast<unsigned long>(delay / 1000));
  health_.reportError(healthId_, 3, msg);
  LOGW(kTag, "%s", msg);
}

void SdStore::onMeasurement(const Measurement& m) {
  if (state_ == State::Off || !everMounted_) return;
  char line[storagefmt::kMaxLineLen];
  const size_t n = storagefmt::formatMeasurement(m, line, sizeof(line));
  if (n == 0) return;
  if (dataLen_ + n > opt_.dataBufferBytes) {
    ++droppedMeasurements_;
    return;
  }
  memcpy(dataBuf_ + dataLen_, line, n);
  dataLen_ += n;
}

void SdStore::write(const LogLine& line) {
  // Before the first mount, lines are only in the log ring; copyLogRing() takes them over.
  if (state_ == State::Off || !everMounted_) return;
  uint32_t utc = 0;
  if (clock_.isSynced()) clock_.toUtc(line.uptimeS, utc);
  char text[storagefmt::kMaxLineLen];
  const size_t n = storagefmt::formatLogLine(line, utc, text, sizeof(text));
  if (n > 0) appendLog(text, n);
}

void SdStore::appendLog(const char* text, size_t len) {
  MutexLock lock(mutex_, kLockTimeoutMs);
  if (!lock.locked()) return;
  size_t& used = logLen_[logActive_];
  if (used + len > opt_.logBufferBytes) {
    ++droppedLogLines_;
    return;
  }
  memcpy(logBuf_[logActive_] + used, text, len);
  used += len;
}

void SdStore::copyLogRing() {
  LogLine line;
  char text[storagefmt::kMaxLineLen];
  for (size_t age = logger::lineCount(); age > 0; --age) {
    if (!logger::lineAt(age - 1, line)) continue;
    uint32_t utc = 0;
    if (clock_.isSynced()) clock_.toUtc(line.uptimeS, utc);
    const size_t n = storagefmt::formatLogLine(line, utc, text, sizeof(text));
    if (n == 0) continue;
    if (logLen_[logActive_] + n > opt_.logBufferBytes) flushLog();  // the ring is larger than one buffer
    appendLog(text, n);
  }
}

bool SdStore::ensureHeader(const char* path, int64_t& size, bool csv) {
  if (size >= 0) return true;
  char header[storagefmt::kMaxLineLen + 64];
  size_t n = storagefmt::formatFileHeader(opt_.firmware, opt_.station, opt_.bootId, header, sizeof(header));
  if (csv && n + strlen(storagefmt::kMeasurementHeader) < sizeof(header)) {
    strcpy(header + n, storagefmt::kMeasurementHeader);
    n += strlen(storagefmt::kMeasurementHeader);
  }
  if (!fs_.append(path, header, n)) return false;
  size = static_cast<int64_t>(n);
  return true;
}

void SdStore::rotate(bool data) {
  if (data) {
    if (dataPart_ < kMaxPart) ++dataPart_;
    storagefmt::filePath(storagefmt::kDataDir, opt_.bootId, dataPart_, "csv", dataPath_, sizeof(dataPath_));
    dataSize_ = -1;
  } else {
    if (logPart_ < kMaxPart) ++logPart_;
    storagefmt::filePath(storagefmt::kLogDir, opt_.bootId, logPart_, "log", logPath_, sizeof(logPath_));
    logSize_ = -1;
  }
}

bool SdStore::flushData() {
  if (dataLen_ == 0) return true;
  if (!ensureHeader(dataPath_, dataSize_, true)) return false;
  if (!fs_.append(dataPath_, dataBuf_, dataLen_)) return false;  // buffer kept for the retry
  dataSize_ += static_cast<int64_t>(dataLen_);
  dataLen_ = 0;
  if (dataSize_ >= static_cast<int64_t>(opt_.maxDataFileBytes)) rotate(true);
  return true;
}

bool SdStore::flushLog() {
  // Swap buffers under the lock, write the full one without it: logging never waits for the card.
  char* buf;
  size_t n;
  {
    MutexLock lock(mutex_, kLockTimeoutMs);
    if (!lock.locked()) return true;
    buf = logBuf_[logActive_];
    n = logLen_[logActive_];
    logActive_ ^= 1;
    logLen_[logActive_] = 0;
  }
  if (n == 0) return true;
  if (!ensureHeader(logPath_, logSize_, false)) return false;
  if (!fs_.append(logPath_, buf, n)) return false;  // these log lines are lost (still on serial)
  logSize_ += static_cast<int64_t>(n);
  if (logSize_ >= static_cast<int64_t>(opt_.maxLogFileBytes)) rotate(false);
  return true;
}

void SdStore::flushNow(uint32_t nowMs) {
  if (state_ != State::Mounted) return;
  lastFlushMs_ = nowMs;
  if (!flushData()) {
    fail("writing measurements", nowMs);
    return;
  }
  if (!flushLog()) fail("writing the log", nowMs);
}

void SdStore::tick(uint32_t nowMs) {
  if (state_ == State::Off) return;

  if (clock_.isSynced()) {
    if (!timeSet_) {
      fs_.setTime(clock_.nowUtc());  // correct file timestamps from now on
      timeSet_ = true;
    }
    if (!syncMarked_ && everMounted_) {
      char marker[storagefmt::kMaxLineLen];
      uint32_t utc = 0;
      const uint32_t up = clock_.uptimeS();
      clock_.toUtc(up, utc);
      const size_t n = storagefmt::formatSyncMarker(up, utc, opt_.bootId, marker, sizeof(marker));
      if (n > 0 && dataLen_ + n <= opt_.dataBufferBytes) {
        memcpy(dataBuf_ + dataLen_, marker, n);
        dataLen_ += n;
        syncMarked_ = true;
      }
    }
  }

  if (state_ == State::Unmounted) {
    if (timeReached(nowMs, retryAtMs_)) tryMount(nowMs);
    return;
  }

  const bool due = nowMs - lastFlushMs_ >= opt_.flushIntervalMs;
  const bool pressure =
      dataLen_ > opt_.dataBufferBytes * 3 / 4 || logLen_[logActive_] > opt_.logBufferBytes * 3 / 4;
  if (due || pressure) {
    lastFlushMs_ = nowMs;
    if (!flushData()) {
      fail("writing measurements", nowMs);
      return;
    }
    if (!flushLog()) {
      fail("writing the log", nowMs);
      return;
    }
    if (!full_) health_.reportSuccess(healthId_, clock_.nowUtc());
  }

  if (timeReached(nowMs, janitorAtMs_)) runJanitor(nowMs);
}

void SdStore::runJanitor(uint32_t nowMs) {
  janitorAtMs_ = nowMs + opt_.janitorIntervalMs;
  uint64_t total = 0;
  uint64_t free = 0;
  if (!fs_.space(total, free)) return;
  totalBytes_ = total;
  freeBytes_ = free;

  ScanCtx logs;
  logs.current = baseName(logPath_);
  ScanCtx data;
  data.current = baseName(dataPath_);
  fs_.list(storagefmt::kLogDir, onFile, &logs);
  fs_.list(storagefmt::kDataDir, onFile, &data);

  char oldestData[storagefmt::kPathLen] = {};
  janitor::Facts facts;
  facts.totalBytes = total;
  facts.freeBytes = free;
  facts.logFiles = logs.count;
  facts.haveOldestLog = logs.have;
  facts.haveOldestData = data.have;
  if (data.have) {
    snprintf(oldestData, sizeof(oldestData), "%s/%s", storagefmt::kDataDir, data.minName);
    facts.oldestDataMtime = fs_.fileMtime(oldestData);
  }
  facts.nowUtc = clock_.nowUtc();

  janitor::Limits limits;
  limits.lowWatermarkPct = opt_.storage.lowWatermarkPct;
  limits.highWatermarkPct = opt_.storage.highWatermarkPct;
  limits.minRetentionDays = opt_.storage.minRetentionDays;
  limits.maxLogFiles = janitor::maxLogFilesFor(total, opt_.maxLogFileBytes);

  char victim[storagefmt::kPathLen] = {};
  switch (janitor::decide(facts, limits, cleaning_)) {
    case janitor::Action::DeleteOldestLog:
      snprintf(victim, sizeof(victim), "%s/%s", storagefmt::kLogDir, logs.minName);
      break;
    case janitor::Action::DeleteOldestData:
      strcpy(victim, oldestData);
      break;
    case janitor::Action::Full:
      if (!full_) {
        full_ = true;
        char msg[ComponentHealth::kErrorLen];
        snprintf(msg, sizeof(msg), "card over %u %% full, nothing left to delete", 100u - opt_.storage.lowWatermarkPct);
        health_.reportError(healthId_, 4, msg);
        LOGE(kTag, "%s (only the current files are left)", msg);
      }
      return;
    case janitor::Action::None:
      if (full_) {
        full_ = false;
        health_.reportSuccess(healthId_, clock_.nowUtc());
      }
      return;
  }

  if (!fs_.remove(victim)) {
    LOGW(kTag, "janitor: could not delete %s", victim);
    return;
  }
  ++filesDeleted_;
  LOGW(kTag, "janitor: deleted %s (%lu MB free of %lu MB)", victim, static_cast<unsigned long>(free >> 20),
       static_cast<unsigned long>(total >> 20));
  janitorAtMs_ = nowMs + opt_.janitorBusyIntervalMs;  // continue soon until enough is free
}

SdStore::Status SdStore::status() const {
  Status s{};
  s.enabled = state_ != State::Off;
  s.mounted = state_ == State::Mounted;
  s.totalBytes = totalBytes_;
  s.freeBytes = freeBytes_;
  s.filesDeleted = filesDeleted_;
  s.droppedMeasurements = droppedMeasurements_;
  {
    MutexLock lock(mutex_, kLockTimeoutMs);
    s.droppedLogLines = droppedLogLines_;
  }
  s.writeErrors = writeErrors_;
  s.full = full_;
  strncpy(s.dataFile, dataPath_, sizeof(s.dataFile) - 1);
  strncpy(s.logFile, logPath_, sizeof(s.logFile) - 1);
  return s;
}
