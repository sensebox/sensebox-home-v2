#pragma once

#include <stddef.h>
#include <stdint.h>

// Structured logging with levels and module tags.
//
//   LOGI("sps30", "fan cleaning started, interval %u days", days);
//
// Every line goes into a RAM ring buffer (readable via REST / diagnostic report) and to all
// registered sinks (serial, SD file, ...). Formatting uses a fixed stack buffer, no heap.
// Not callable from ISRs.

enum class LogLevel : uint8_t { Error = 0, Warn, Info, Debug };

struct LogLine {
  static constexpr size_t kTagLen = 12;
  static constexpr size_t kMsgLen = 100;

  uint32_t uptimeS;
  LogLevel level;
  char tag[kTagLen];
  char msg[kMsgLen];
};

class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual void write(const LogLine& line) = 0;
};

namespace logger {

constexpr size_t kRingLines = 128;
constexpr size_t kMaxSinks = 3;

using UptimeMsFn = uint64_t (*)();

void init(UptimeMsFn uptimeMs);
void setLevel(LogLevel level);
LogLevel level();
bool addSink(LogSink* sink);
void removeSink(LogSink* sink);

void log(LogLevel level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

// Copies up to `max` of the most recent lines, oldest first. Returns the number copied.
size_t copyRecent(LogLine* out, size_t max);
// Total number of lines dropped because the ring was full (overwritten) or the lock timed out.
uint32_t droppedLines();

const char* levelName(LogLevel level);
char levelLetter(LogLevel level);

}  // namespace logger

#define LOGE(tag, ...) ::logger::log(LogLevel::Error, tag, __VA_ARGS__)
#define LOGW(tag, ...) ::logger::log(LogLevel::Warn, tag, __VA_ARGS__)
#define LOGI(tag, ...) ::logger::log(LogLevel::Info, tag, __VA_ARGS__)
#define LOGD(tag, ...) ::logger::log(LogLevel::Debug, tag, __VA_ARGS__)
