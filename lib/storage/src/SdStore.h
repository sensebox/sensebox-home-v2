#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Clock.h"
#include "FileSystem.h"
#include "Health.h"
#include "Janitor.h"
#include "Log.h"
#include "MeasurementSink.h"
#include "Mutex.h"
#include "StationConfig.h"
#include "StorageFormat.h"

// Measurement history and log files on the SD card (plan §4.5, §4.5.1).
//
// - Every measurement (raw, corrected, 10-min means) is appended as a CSV line to /data, every
//   log line to /logs (see StorageFormat.h for names and formats).
// - Writes are collected in small RAM buffers and flushed every few seconds, so the main loop
//   isn't blocked by every single SD access. Up to one flush interval can be lost on a crash.
// - The card is optional: without one (config "sd": "auto") the box simply measures without
//   history. A card that fails or is removed is unmounted, powered off and retried with backoff;
//   measurements arriving meanwhile are kept while they fit into the buffer.
// - The janitor (Janitor.h) keeps the card from overflowing by deleting the oldest files.
//
// Threading: everything runs in the main loop, except write(LogLine), which may come from any
// task (it only appends to a buffer under a mutex). Never log while holding mutex_.

class SdStore : public MeasurementSink, public LogSink {
 public:
  struct Options {
    uint32_t bootId = 0;
    config::StorageConfig storage;
    const char* firmware = "";
    const char* station = "";
    uint32_t maxDataFileBytes = 4u * 1024u * 1024u;
    uint32_t maxLogFileBytes = 1024u * 1024u;
    size_t dataBufferBytes = 2048;
    size_t logBufferBytes = 1024;  // two of them (double buffering)
    uint32_t flushIntervalMs = 15000;
    uint32_t janitorIntervalMs = 10u * 60u * 1000u;
    uint32_t janitorBusyIntervalMs = 200;  // between deletions while cleaning up
    uint32_t retryBaseMs = 30000;
    uint32_t retryMaxMs = 10u * 60u * 1000u;
  };

  struct Status {
    bool enabled;
    bool mounted;
    uint64_t totalBytes;  // as of the last janitor run
    uint64_t freeBytes;
    uint32_t filesDeleted;
    uint32_t droppedMeasurements;  // lines that didn't fit into the buffer
    uint32_t droppedLogLines;
    uint32_t writeErrors;
    bool full;
    char dataFile[storagefmt::kPathLen];
    char logFile[storagefmt::kPathLen];
  };

  SdStore(FileSystem& fs, Clock& clock, HealthRegistry& health);
  ~SdStore() override;
  SdStore(const SdStore&) = delete;
  SdStore& operator=(const SdStore&) = delete;

  // Allocates the buffers (heap, once) and tries the first mount. Returns false only if the
  // store is disabled ("sd": "off") or out of memory; a missing card is not an error.
  bool begin(const Options& options, uint32_t nowMs);
  void tick(uint32_t nowMs);
  // Writes everything buffered now, e.g. right before a restart.
  void flushNow(uint32_t nowMs);

  void onMeasurement(const Measurement& m) override;
  void write(const LogLine& line) override;

  Status status() const;

 private:
  enum class State : uint8_t { Off, Unmounted, Mounted };

  void tryMount(uint32_t nowMs);
  void fail(const char* what, uint32_t nowMs);
  bool flushData();
  bool flushLog();
  bool ensureHeader(const char* path, int64_t& size, bool csv);
  void rotate(bool data);
  void runJanitor(uint32_t nowMs);
  void copyLogRing();
  void appendLog(const char* text, size_t len);

  FileSystem& fs_;
  Clock& clock_;
  HealthRegistry& health_;
  HealthRegistry::Id healthId_;
  Options opt_;
  State state_ = State::Off;
  bool everMounted_ = false;
  bool syncMarked_ = false;
  bool timeSet_ = false;

  char* dataBuf_ = nullptr;
  size_t dataLen_ = 0;
  char* logBuf_[2] = {nullptr, nullptr};
  size_t logLen_[2] = {0, 0};
  uint8_t logActive_ = 0;
  mutable Mutex mutex_;  // guards logBuf_/logLen_/logActive_/droppedLogLines_

  char dataPath_[storagefmt::kPathLen] = {};
  char logPath_[storagefmt::kPathLen] = {};
  uint16_t dataPart_ = 0;
  uint16_t logPart_ = 0;
  int64_t dataSize_ = -1;  // -1: file doesn't exist yet (header needed)
  int64_t logSize_ = -1;

  uint32_t lastFlushMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint8_t retryAttempt_ = 0;
  uint32_t janitorAtMs_ = 0;
  bool cleaning_ = false;
  bool full_ = false;

  uint64_t totalBytes_ = 0;
  uint64_t freeBytes_ = 0;
  uint32_t filesDeleted_ = 0;
  uint32_t droppedMeasurements_ = 0;
  uint32_t droppedLogLines_ = 0;
  uint32_t writeErrors_ = 0;
};
