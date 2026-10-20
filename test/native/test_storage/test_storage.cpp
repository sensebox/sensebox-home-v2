#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <map>
#include <algorithm>
#include <string>

#include "Janitor.h"
#include "SdStore.h"
#include "StorageFormat.h"

namespace {

uint64_t g_uptimeMs = 0;
uint64_t fakeUptimeMs() { return g_uptimeMs; }
uint32_t fakeUptimeS() { return static_cast<uint32_t>(g_uptimeMs / 1000); }

// In-memory SD card. Free space = capacity - sum of file sizes.
class FakeFs : public FileSystem {
 public:
  bool mount() override {
    ++mounts;
    mounted = cardPresent;
    return mounted;
  }
  void unmount() override {
    mounted = false;
    ++unmounts;
  }
  bool ensureDir(const char*) override { return mounted; }
  bool append(const char* path, const char* data, size_t len) override {
    if (!mounted || failWrites) return false;
    files[path].data.append(data, len);
    if (files[path].mtime == 0) files[path].mtime = clockUtc;
    return true;
  }
  int64_t fileSize(const char* path) override {
    auto it = files.find(path);
    return it == files.end() ? -1 : static_cast<int64_t>(it->second.data.size());
  }
  int64_t read(const char* path, uint64_t offset, char* buf, size_t len) override {
    auto it = files.find(path);
    if (!mounted || it == files.end()) return -1;
    const std::string& d = it->second.data;
    if (offset > d.size()) return 0;
    const size_t n = std::min(len, d.size() - static_cast<size_t>(offset));
    memcpy(buf, d.data() + offset, n);
    return static_cast<int64_t>(n);
  }
  uint32_t fileMtime(const char* path) override {
    auto it = files.find(path);
    return it == files.end() ? 0 : it->second.mtime;
  }
  bool remove(const char* path) override { return mounted && files.erase(path) == 1; }
  bool list(const char* dir, ListFn fn, void* ctx) override {
    if (!mounted) return false;
    const std::string prefix = std::string(dir) + "/";
    for (auto& kv : files) {
      if (kv.first.compare(0, prefix.size(), prefix) == 0) fn(kv.first.c_str() + prefix.size(), ctx);
    }
    return true;
  }
  bool space(uint64_t& total, uint64_t& free) override {
    if (!mounted) return false;
    total = capacity;
    uint64_t used = 0;
    for (auto& kv : files) used += kv.second.data.size();
    free = used > capacity ? 0 : capacity - used;
    return true;
  }
  void setTime(uint32_t utc) override { clockUtc = utc; }

  // Adds an "old" file with `size` bytes.
  void add(const std::string& path, size_t size, uint32_t mtime = 0) {
    files[path].data.assign(size, 'x');
    files[path].mtime = mtime;
  }
  std::string content(const std::string& path) { return files.count(path) ? files[path].data : std::string(); }

  struct File {
    std::string data;
    uint32_t mtime = 0;
  };
  std::map<std::string, File> files;
  bool cardPresent = true;
  bool mounted = false;
  bool failWrites = false;
  uint64_t capacity = 64ull * 1024 * 1024;
  uint32_t clockUtc = 0;
  int mounts = 0;
  int unmounts = 0;
};

Measurement meas(Phenomenon p, float v, uint32_t ts, uint16_t q = 0) {
  Measurement m{};
  m.timestamp = ts;
  m.value = v;
  m.sensorSlot = 0;
  m.phenomenon = p;
  m.unit = defaultUnit(p);
  m.kind = ValueKind::Raw;
  m.quality = q;
  return m;
}

SdStore::Options testOptions() {
  SdStore::Options o;
  o.bootId = 42;
  o.firmware = "0.1.0-test";
  o.station = "test-box";
  o.flushIntervalMs = 10000;
  return o;
}

void advance(SdStore& sd, uint32_t ms) {
  const uint64_t end = g_uptimeMs + ms;
  while (g_uptimeMs < end) {
    g_uptimeMs += 1000;
    sd.tick(static_cast<uint32_t>(g_uptimeMs));
  }
}

size_t countLines(const std::string& s, const char* needle) {
  size_t n = 0;
  for (size_t pos = s.find(needle); pos != std::string::npos; pos = s.find(needle, pos + 1)) ++n;
  return n;
}

}  // namespace

void setUp() {
  g_uptimeMs = 0;
  logger::init(fakeUptimeMs);
  logger::setLevel(LogLevel::Info);
}
void tearDown() {}

// --- formats ------------------------------------------------------------------

void test_iso_time() {
  char buf[24];
  TEST_ASSERT_EQUAL(20, storagefmt::formatIsoUtc(0, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00Z", buf);
  storagefmt::formatIsoUtc(1709164800u, buf, sizeof(buf));  // leap day
  TEST_ASSERT_EQUAL_STRING("2024-02-29T00:00:00Z", buf);
  storagefmt::formatIsoUtc(1791970245u, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("2026-10-14T09:30:45Z", buf);
}

void test_measurement_line() {
  char buf[160];
  storagefmt::formatMeasurement(meas(Phenomenon::AirTemperature, 23.456f, 1791970245u), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("2026-10-14T09:30:45Z,,0,air_temperature,raw,23.46,°C,0x000\n", buf);

  storagefmt::formatMeasurement(meas(Phenomenon::PM2_5, 4.0f, 125, quality::kTimeUnsynced), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING(",125,0,pm2_5,raw,4.00,µg/m³,0x040\n", buf);

  storagefmt::formatMeasurement(meas(Phenomenon::PM10, NAN, 125, quality::kTimeUnsynced), buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, ",nan,"));
  TEST_ASSERT_EQUAL(0, storagefmt::formatMeasurement(meas(Phenomenon::PM10, 1.0f, 1), buf, 10));  // doesn't fit
}

void test_log_line() {
  LogLine line{};
  line.uptimeS = 123;
  line.level = LogLevel::Warn;
  strcpy(line.tag, "sensors");
  strcpy(line.msg, "slot 0: retry");
  char buf[160];
  storagefmt::formatLogLine(line, 0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("-                    [    123] W sensors  slot 0: retry\n", buf);
  storagefmt::formatLogLine(line, 1791970245u, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("2026-10-14T09:30:45Z [    123] W sensors  slot 0: retry\n", buf);
}

void test_file_names() {
  char path[40];
  storagefmt::filePath("/data", 42, 3, "csv", path, sizeof(path));
  TEST_ASSERT_EQUAL_STRING("/data/B000042-0003.csv", path);
  uint32_t boot;
  uint16_t part;
  TEST_ASSERT_TRUE(storagefmt::parseFileName("B000042-0003.csv", boot, part));
  TEST_ASSERT_EQUAL_UINT32(42, boot);
  TEST_ASSERT_EQUAL_UINT16(3, part);
  TEST_ASSERT_FALSE(storagefmt::parseFileName("notes.txt", boot, part));
  TEST_ASSERT_FALSE(storagefmt::parseFileName("B00004X-0003.csv", boot, part));
  TEST_ASSERT_FALSE(storagefmt::parseFileName("B000042_0003.csv", boot, part));
  TEST_ASSERT_TRUE(storagefmt::fileKey(41, 9999) < storagefmt::fileKey(42, 1));
}

// --- janitor policy -----------------------------------------------------------

void test_janitor_watermarks_and_hysteresis() {
  janitor::Limits lim;  // 10 % / 20 %
  janitor::Facts f;
  f.totalBytes = 1000ull * 1024 * 1024;
  f.haveOldestLog = false;
  f.haveOldestData = true;
  bool cleaning = false;

  f.freeBytes = f.totalBytes / 100 * 15;  // between the marks, not cleaning: nothing
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::None), static_cast<int>(janitor::decide(f, lim, cleaning)));

  f.freeBytes = f.totalBytes / 100 * 5;  // below low: start
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::DeleteOldestData),
                    static_cast<int>(janitor::decide(f, lim, cleaning)));
  TEST_ASSERT_TRUE(cleaning);

  f.freeBytes = f.totalBytes / 100 * 15;  // between: continue until high
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::DeleteOldestData),
                    static_cast<int>(janitor::decide(f, lim, cleaning)));

  f.freeBytes = f.totalBytes / 100 * 20;  // reached high: stop
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::None), static_cast<int>(janitor::decide(f, lim, cleaning)));
  TEST_ASSERT_FALSE(cleaning);
}

void test_janitor_logs_first_and_log_cap() {
  janitor::Limits lim;
  lim.maxLogFiles = 1000;
  lim.logFileBytes = 1024 * 1024;
  lim.maxLogBytes = 5ull * 1024 * 1024;
  janitor::Facts f;
  f.totalBytes = 1000ull * 1024 * 1024;
  f.freeBytes = f.totalBytes / 2;  // plenty of space
  f.haveOldestLog = true;
  f.haveOldestData = true;
  bool cleaning = false;

  f.logFiles = 8;
  f.rotatedLogFiles = 5;
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::None), static_cast<int>(janitor::decide(f, lim, cleaning)));
  f.rotatedLogFiles = 6;  // over the byte cap: delete a log even though space is fine
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::DeleteOldestLog),
                    static_cast<int>(janitor::decide(f, lim, cleaning)));

  // Reboot loop: many tiny files, one per boot. Kept up to the file cap.
  f.rotatedLogFiles = 0;
  f.logFiles = 1000;
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::None), static_cast<int>(janitor::decide(f, lim, cleaning)));
  f.logFiles = 1001;
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::DeleteOldestLog),
                    static_cast<int>(janitor::decide(f, lim, cleaning)));

  f.logFiles = 2;
  f.freeBytes = f.totalBytes / 100;  // low space: logs go first
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::DeleteOldestLog),
                    static_cast<int>(janitor::decide(f, lim, cleaning)));

  const uint64_t mb = 1024ull * 1024;
  TEST_ASSERT_EQUAL_UINT64(100 * mb, janitor::maxLogBytesFor(4000 * mb, 1024 * 1024));  // 100 MB cap
  TEST_ASSERT_EQUAL_UINT64(512 * mb / 10, janitor::maxLogBytesFor(512 * mb, 1024 * 1024));  // 10 %
  TEST_ASSERT_EQUAL_UINT64(4 * mb, janitor::maxLogBytesFor(8 * mb, 1024 * 1024));  // minimum
}

void test_janitor_90_percent_is_a_hard_limit_retention_only_below() {
  janitor::Limits lim;  // 10 % / 20 %, 30 days
  janitor::Facts f;
  f.totalBytes = 1000ull * 1024 * 1024;
  f.haveOldestData = true;
  f.nowUtc = 1791970245u;
  f.oldestDataMtime = f.nowUtc - 10 * 86400;  // 10 days old
  bool cleaning = false;

  // Over 90 % full: deleted although it is younger than the retention time.
  f.freeBytes = f.totalBytes / 100 * 9;
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::DeleteOldestData),
                    static_cast<int>(janitor::decide(f, lim, cleaning)));

  // Back under 90 %, still cleaning towards 80 %: young files are kept now.
  f.freeBytes = f.totalBytes / 100 * 12;
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::None), static_cast<int>(janitor::decide(f, lim, cleaning)));

  f.oldestDataMtime = f.nowUtc - 40 * 86400;  // 40 days: may go
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::DeleteOldestData),
                    static_cast<int>(janitor::decide(f, lim, cleaning)));

  f.oldestDataMtime = 0;  // age unknown (written before the clock was set): may go
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::DeleteOldestData),
                    static_cast<int>(janitor::decide(f, lim, cleaning)));

  // Nothing deletable: over 90 % that's "full", between the watermarks it's fine.
  f.haveOldestData = false;
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::None), static_cast<int>(janitor::decide(f, lim, cleaning)));
  f.freeBytes = f.totalBytes / 100 * 9;
  TEST_ASSERT_EQUAL(static_cast<int>(janitor::Action::Full), static_cast<int>(janitor::decide(f, lim, cleaning)));
}

// --- SdStore ---------------------------------------------------------------------

void test_writes_measurements_and_log_with_headers() {
  FakeFs fs;
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  LOGI("main", "boot line before the card");  // in the ring, copied at mount
  TEST_ASSERT_TRUE(logger::addSink(&sd));
  TEST_ASSERT_TRUE(sd.begin(testOptions(), 0));
  TEST_ASSERT_TRUE(sd.status().mounted);

  sd.onMeasurement(meas(Phenomenon::AirTemperature, 21.0f, 60, quality::kTimeUnsynced));
  LOGI("test", "hello sd");
  TEST_ASSERT_EQUAL(-1, fs.fileSize("/data/B000042-0001.csv"));  // buffered, not written yet
  advance(sd, 11000);

  const std::string data = fs.content("/data/B000042-0001.csv");
  TEST_ASSERT_EQUAL(0u, data.find("# senseBox:home v2 firmware 0.1.0-test, station \"test-box\", boot 42\n"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, data.find(storagefmt::kMeasurementHeader));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, data.find(",60,0,air_temperature,raw,21.00,°C,0x040\n"));

  const std::string log = fs.content("/logs/B000042-0001.log");
  TEST_ASSERT_NOT_EQUAL(std::string::npos, log.find("boot line before the card"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, log.find("hello sd"));
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Ok), static_cast<int>(health.overall()));
  logger::removeSink(&sd);
}

void test_sync_marker_and_utc_lines() {
  FakeFs fs;
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  sd.begin(testOptions(), 0);
  g_uptimeMs = 120000;
  clock.setUtc(1791970245000ull, g_uptimeMs, TimeSource::LoRaWan);
  advance(sd, 11000);
  sd.onMeasurement(meas(Phenomenon::RelativeHumidity, 55.0f, clock.nowUtc()));
  advance(sd, 11000);
  const std::string data = fs.content("/data/B000042-0001.csv");
  TEST_ASSERT_EQUAL(1u, countLines(data, "# time synced: uptime_s "));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, data.find("2026-10-14T09:30:5"));
  TEST_ASSERT_EQUAL_UINT32(1791970245u + 1, fs.clockUtc);  // file timestamps set
}

void test_rotation() {
  FakeFs fs;
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  SdStore::Options o = testOptions();
  o.maxDataFileBytes = 600;
  sd.begin(o, 0);
  for (int i = 0; i < 30; ++i) {
    sd.onMeasurement(meas(Phenomenon::PM10, static_cast<float>(i), 60 + i, quality::kTimeUnsynced));
    advance(sd, 11000);
  }
  TEST_ASSERT_TRUE(fs.fileSize("/data/B000042-0001.csv") >= 600);
  TEST_ASSERT_TRUE(fs.fileSize("/data/B000042-0002.csv") > 0);
  // every part starts with its own header
  TEST_ASSERT_EQUAL(0u, fs.content("/data/B000042-0002.csv").find("# senseBox:home v2"));
}

void test_new_boot_never_overwrites() {
  FakeFs fs;
  fs.add("/data/B000042-0001.csv", 10);
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  sd.begin(testOptions(), 0);
  TEST_ASSERT_EQUAL_STRING("/data/B000042-0002.csv", sd.status().dataFile);
}

void test_missing_card_is_a_fault_and_data_waits_for_it() {
  FakeFs fs;
  fs.cardPresent = false;
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  TEST_ASSERT_TRUE(sd.begin(testOptions(), 0));
  TEST_ASSERT_FALSE(sd.status().mounted);
  // The card is part of the hardware: missing = error (LED red), not "disabled".
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Error), static_cast<int>(health.overall()));
  sd.onMeasurement(meas(Phenomenon::PM10, 1.5f, 5, quality::kTimeUnsynced));  // buffered meanwhile
  TEST_ASSERT_EQUAL_UINT32(0, sd.status().droppedMeasurements);

  fs.cardPresent = true;
  advance(sd, 31000);  // first retry after 30 s, then the first flush
  TEST_ASSERT_TRUE(sd.status().mounted);
  advance(sd, 11000);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, fs.content("/data/B000042-0001.csv").find(",5,0,pm10,raw,1.50"));
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Ok), static_cast<int>(health.overall()));
}

void test_write_error_remounts_and_keeps_buffered_data() {
  FakeFs fs;
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  sd.begin(testOptions(), 0);

  fs.failWrites = true;
  sd.onMeasurement(meas(Phenomenon::PM10, 7.5f, 70, quality::kTimeUnsynced));
  advance(sd, 11000);
  TEST_ASSERT_FALSE(sd.status().mounted);
  TEST_ASSERT_EQUAL(1, fs.unmounts);
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Error), static_cast<int>(health.overall()));

  sd.onMeasurement(meas(Phenomenon::PM10, 8.5f, 80, quality::kTimeUnsynced));  // buffered meanwhile
  fs.failWrites = false;
  advance(sd, 15000);  // remount after 2 s, flush
  TEST_ASSERT_TRUE(sd.status().mounted);
  const std::string data = fs.content("/data/B000042-0001.csv");
  TEST_ASSERT_NOT_EQUAL(std::string::npos, data.find(",70,0,pm10,raw,7.50"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, data.find(",80,0,pm10,raw,8.50"));
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Ok), static_cast<int>(health.overall()));
}

void test_card_removed_permanently_retries_with_backoff() {
  FakeFs fs;
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  sd.begin(testOptions(), 0);
  fs.cardPresent = false;
  fs.failWrites = true;
  sd.onMeasurement(meas(Phenomenon::PM10, 1.0f, 1, quality::kTimeUnsynced));
  advance(sd, 11000);
  const int mountsAfterFail = fs.mounts;
  advance(sd, 10u * 60u * 1000u);
  // 2 s, then 30 s, 60 s, 120 s, 240 s: a handful of attempts in 10 minutes, not hundreds
  TEST_ASSERT_TRUE(fs.mounts - mountsAfterFail <= 6);
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Error), static_cast<int>(health.overall()));
}

void test_buffer_overflow_counts_dropped() {
  FakeFs fs;
  fs.cardPresent = true;
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  SdStore::Options o = testOptions();
  o.dataBufferBytes = 200;
  sd.begin(o, 0);
  fs.failWrites = true;
  advance(sd, 11000);  // fails, unmounted; buffer can't be written
  for (int i = 0; i < 10; ++i) sd.onMeasurement(meas(Phenomenon::PM10, 1.0f, 1, quality::kTimeUnsynced));
  TEST_ASSERT_TRUE(sd.status().droppedMeasurements > 0);
}

void test_log_tail_comes_from_the_card() {
  FakeFs fs;
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  sd.begin(testOptions(), 0);
  sd.flushNow(100);  // the boot lines copied from the RAM ring at mount
  for (int i = 0; i < 30; ++i) {
    LogLine line{};
    line.uptimeS = static_cast<uint32_t>(i);
    line.level = LogLevel::Info;
    strcpy(line.tag, "test");
    snprintf(line.msg, sizeof(line.msg), "line number %02d", i);
    sd.write(line);
    if (i % 5 == 0) sd.flushNow(500);  // as the main loop does: the RAM buffer is only ~1 KB
  }
  TEST_ASSERT_EQUAL_UINT32(0, sd.status().droppedLogLines);
  // Not flushed yet: readLogTail() flushes first, so the newest line is there.
  char buf[300];
  const size_t n = sd.readLogTail(1000, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0 && n < sizeof(buf));
  TEST_ASSERT_EQUAL(n, strlen(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "line number 29\n"));
  TEST_ASSERT_NULL(strstr(buf, "line number 00"));  // older lines don't fit into 300 bytes
  // Starts at a line boundary: the first character is the start of a log line.
  TEST_ASSERT_EQUAL_CHAR('-', buf[0]);

  // Whole file fits: everything, header included.
  char big[8192];
  sd.readLogTail(2000, big, sizeof(big));
  TEST_ASSERT_EQUAL_CHAR('#', big[0]);
  TEST_ASSERT_NOT_NULL(strstr(big, "line number 00"));

  // Card gone: the flush before reading fails, the card is unmounted, nothing is returned.
  fs.cardPresent = false;
  fs.failWrites = true;
  LogLine last{};
  last.level = LogLevel::Info;
  strcpy(last.tag, "test");
  strcpy(last.msg, "after the card was pulled");
  sd.write(last);
  TEST_ASSERT_EQUAL(0, sd.readLogTail(3000, buf, sizeof(buf)));
  TEST_ASSERT_FALSE(sd.status().mounted);
}

void test_small_card_is_flagged() {
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  FakeFs small;  // 64 MB
  SdStore a(small, clock, health);
  a.begin(testOptions(), 0);
  TEST_ASSERT_TRUE(a.status().smallCard);

  FakeFs card4g;
  card4g.capacity = 3965ull * 1024 * 1024;  // what a "4 GB" card reports
  SdStore b(card4g, clock, health);
  b.begin(testOptions(), 0);
  TEST_ASSERT_FALSE(b.status().smallCard);
}

void test_janitor_frees_space_oldest_first() {
  FakeFs fs;
  fs.capacity = 100ull * 1024 * 1024;  // 100 MB card
  // 95 MB of old measurement files from earlier boots, and some old logs
  for (int b = 1; b <= 19; ++b) {
    char path[40];
    storagefmt::filePath("/data", static_cast<uint32_t>(b), 1, "csv", path, sizeof(path));
    fs.add(path, 5u * 1024 * 1024);
  }
  fs.add("/logs/B000003-0001.log", 1000);
  fs.add("/logs/B000002-0001.log", 1000);
  fs.add("/notes.txt", 10);  // not ours: never deleted

  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  sd.begin(testOptions(), 0);
  advance(sd, 30000);

  uint64_t total, free;
  fs.space(total, free);
  TEST_ASSERT_TRUE(free >= total / 100 * 20);  // back at the high watermark
  TEST_ASSERT_EQUAL(-1, fs.fileSize("/logs/B000002-0001.log"));  // logs went first
  TEST_ASSERT_EQUAL(-1, fs.fileSize("/data/B000001-0001.csv"));  // then the oldest data
  TEST_ASSERT_TRUE(fs.fileSize("/data/B000019-0001.csv") > 0);   // newest data kept
  TEST_ASSERT_TRUE(fs.fileSize("/notes.txt") > 0);
  TEST_ASSERT_TRUE(sd.status().filesDeleted >= 4);
}

void test_janitor_never_deletes_current_files_and_reports_full() {
  FakeFs fs;
  fs.capacity = 10ull * 1024 * 1024;
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  sd.begin(testOptions(), 0);
  // Fill the card with the current file itself.
  fs.add("/data/B000042-0001.csv", 9u * 1024 * 1024 + 900u * 1024);
  advance(sd, 11u * 60u * 1000u);  // next periodic janitor run
  TEST_ASSERT_TRUE(fs.fileSize("/data/B000042-0001.csv") > 0);
  TEST_ASSERT_TRUE(sd.status().full);
  TEST_ASSERT_EQUAL(static_cast<int>(ComponentStatus::Error), static_cast<int>(health.overall()));
}

void test_janitor_keeps_tiny_logs_of_a_reboot_loop() {
  FakeFs fs;
  fs.capacity = 4000ull * 1024 * 1024;
  // 350 boots that each left a ~1 KB log (the old count cap kept only 100).
  for (uint32_t b = 1; b <= 350; ++b) {
    char path[40];
    storagefmt::filePath("/logs", b, 1, "log", path, sizeof(path));
    fs.add(path, 1000);
  }
  Clock clock(fakeUptimeMs);
  HealthRegistry health(fakeUptimeS);
  SdStore sd(fs, clock, health);
  sd.begin(testOptions(), 0);
  advance(sd, 30000);
  TEST_ASSERT_TRUE(fs.fileSize("/logs/B000001-0001.log") > 0);  // nothing deleted
  TEST_ASSERT_EQUAL_UINT32(0, sd.status().filesDeleted);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_iso_time);
  RUN_TEST(test_measurement_line);
  RUN_TEST(test_log_line);
  RUN_TEST(test_file_names);
  RUN_TEST(test_janitor_watermarks_and_hysteresis);
  RUN_TEST(test_janitor_logs_first_and_log_cap);
  RUN_TEST(test_janitor_90_percent_is_a_hard_limit_retention_only_below);
  RUN_TEST(test_writes_measurements_and_log_with_headers);
  RUN_TEST(test_sync_marker_and_utc_lines);
  RUN_TEST(test_rotation);
  RUN_TEST(test_new_boot_never_overwrites);
  RUN_TEST(test_missing_card_is_a_fault_and_data_waits_for_it);
  RUN_TEST(test_write_error_remounts_and_keeps_buffered_data);
  RUN_TEST(test_card_removed_permanently_retries_with_backoff);
  RUN_TEST(test_buffer_overflow_counts_dropped);
  RUN_TEST(test_small_card_is_flagged);
  RUN_TEST(test_log_tail_comes_from_the_card);
  RUN_TEST(test_janitor_frees_space_oldest_first);
  RUN_TEST(test_janitor_never_deletes_current_files_and_reports_full);
  RUN_TEST(test_janitor_keeps_tiny_logs_of_a_reboot_loop);
  return UNITY_END();
}
