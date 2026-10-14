#include "Log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Mutex.h"
#include "RingBuffer.h"

namespace logger {
namespace {

constexpr uint32_t kLockTimeoutMs = 50;

uint64_t noUptime() { return 0; }

UptimeMsFn g_uptimeMs = noUptime;
LogLevel g_level = LogLevel::Info;
LogSink* g_sinks[kMaxSinks] = {};
RingBuffer<LogLine, kRingLines> g_ring;
uint32_t g_lockDrops = 0;
Mutex g_mutex;

}  // namespace

void init(UptimeMsFn uptimeMs) { g_uptimeMs = uptimeMs != nullptr ? uptimeMs : noUptime; }

void setLevel(LogLevel level) { g_level = level; }

LogLevel level() { return g_level; }

bool addSink(LogSink* sink) {
  MutexLock lock(g_mutex, kLockTimeoutMs);
  if (!lock.locked()) return false;
  for (auto& slot : g_sinks) {
    if (slot == sink) return true;
    if (slot == nullptr) {
      slot = sink;
      return true;
    }
  }
  return false;
}

void removeSink(LogSink* sink) {
  MutexLock lock(g_mutex, kLockTimeoutMs);
  if (!lock.locked()) return;
  for (auto& slot : g_sinks) {
    if (slot == sink) slot = nullptr;
  }
}

void log(LogLevel level, const char* tag, const char* fmt, ...) {
  if (static_cast<uint8_t>(level) > static_cast<uint8_t>(g_level)) return;

  LogLine line;
  line.uptimeS = static_cast<uint32_t>(g_uptimeMs() / 1000);
  line.level = level;
  strncpy(line.tag, tag != nullptr ? tag : "-", LogLine::kTagLen - 1);
  line.tag[LogLine::kTagLen - 1] = '\0';

  va_list args;
  va_start(args, fmt);
  vsnprintf(line.msg, LogLine::kMsgLen, fmt, args);
  va_end(args);

  MutexLock lock(g_mutex, kLockTimeoutMs);
  if (!lock.locked()) {
    ++g_lockDrops;
    return;
  }
  g_ring.push(line);
  for (auto* sink : g_sinks) {
    if (sink != nullptr) sink->write(line);
  }
}

size_t copyRecent(LogLine* out, size_t max) {
  MutexLock lock(g_mutex, kLockTimeoutMs);
  if (!lock.locked() || out == nullptr) return 0;
  const size_t n = g_ring.size() < max ? g_ring.size() : max;
  const size_t start = g_ring.size() - n;
  for (size_t i = 0; i < n; ++i) out[i] = g_ring.at(start + i);
  return n;
}

size_t lineCount() {
  MutexLock lock(g_mutex, kLockTimeoutMs);
  return lock.locked() ? g_ring.size() : 0;
}

bool lineAt(size_t age, LogLine& out) {
  MutexLock lock(g_mutex, kLockTimeoutMs);
  if (!lock.locked() || age >= g_ring.size()) return false;
  out = g_ring.at(g_ring.size() - 1 - age);
  return true;
}

uint32_t droppedLines() { return static_cast<uint32_t>(g_ring.overwritten()) + g_lockDrops; }

const char* levelName(LogLevel level) {
  switch (level) {
    case LogLevel::Error: return "error";
    case LogLevel::Warn: return "warn";
    case LogLevel::Info: return "info";
    case LogLevel::Debug: return "debug";
  }
  return "?";
}

char levelLetter(LogLevel level) {
  switch (level) {
    case LogLevel::Error: return 'E';
    case LogLevel::Warn: return 'W';
    case LogLevel::Info: return 'I';
    case LogLevel::Debug: return 'D';
  }
  return '?';
}

}  // namespace logger
