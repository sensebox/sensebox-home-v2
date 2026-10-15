#include "SdCardFs.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "Clock.h"

namespace {

constexpr const char* kMount = "/sd";
constexpr uint32_t kSpiHz = 8000000;
constexpr uint8_t kMaxOpenFiles = 2;  // each open file costs heap
constexpr uint32_t kPowerUpMs = 50;

SPIClass g_spi(HSPI);  // the default SPI bus belongs to the LoRa radio

// "/data/x.csv" -> "/sd/data/x.csv"
void fullPath(const char* path, char* out, size_t cap) { snprintf(out, cap, "%s%s", kMount, path); }

void power(bool on) {
  pinMode(PIN_SD_ENABLE, OUTPUT);
  digitalWrite(PIN_SD_ENABLE, on ? LOW : HIGH);
}

}  // namespace

bool SdCardFs::mount() {
  if (mounted_) return true;
  power(true);
  delay(kPowerUpMs);
  g_spi.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, g_spi, kSpiHz, kMount, kMaxOpenFiles, false) || SD.cardType() == CARD_NONE) {
    SD.end();
    g_spi.end();
    power(false);
    return false;
  }
  mounted_ = true;
  return true;
}

void SdCardFs::unmount() {
  if (mounted_) SD.end();
  g_spi.end();
  power(false);  // a later mount() starts the card from a clean power-up
  mounted_ = false;
}

bool SdCardFs::ensureDir(const char* path) {
  if (!mounted_) return false;
  char full[64];
  fullPath(path, full, sizeof(full));
  struct stat st;
  if (stat(full, &st) == 0) return S_ISDIR(st.st_mode);
  return mkdir(full, 0775) == 0 || errno == EEXIST;
}

bool SdCardFs::append(const char* path, const char* data, size_t len) {
  if (!mounted_) return false;
  char full[64];
  fullPath(path, full, sizeof(full));
  FILE* f = fopen(full, "a");
  if (f == nullptr) return false;
  const size_t written = fwrite(data, 1, len, f);
  // fclose() flushes and updates the directory entry: after it, the data survives a power cut.
  const bool closed = fclose(f) == 0;
  return written == len && closed;
}

int64_t SdCardFs::fileSize(const char* path) {
  if (!mounted_) return -1;
  char full[64];
  fullPath(path, full, sizeof(full));
  struct stat st;
  if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) return -1;
  return static_cast<int64_t>(st.st_size);
}

int64_t SdCardFs::read(const char* path, uint64_t offset, char* buf, size_t len) {
  if (!mounted_) return -1;
  char full[64];
  fullPath(path, full, sizeof(full));
  FILE* f = fopen(full, "r");
  if (f == nullptr) return -1;
  int64_t n = -1;
  if (fseek(f, static_cast<long>(offset), SEEK_SET) == 0) n = static_cast<int64_t>(fread(buf, 1, len, f));
  fclose(f);
  return n;
}

uint32_t SdCardFs::fileMtime(const char* path) {
  if (!mounted_) return 0;
  char full[64];
  fullPath(path, full, sizeof(full));
  struct stat st;
  if (stat(full, &st) != 0) return 0;
  // Files written before the clock was set carry FAT's default date (1980): unknown.
  const uint32_t t = static_cast<uint32_t>(st.st_mtime);
  return t >= Clock::kMinValidUtc ? t : 0;
}

bool SdCardFs::remove(const char* path) {
  if (!mounted_) return false;
  char full[64];
  fullPath(path, full, sizeof(full));
  return unlink(full) == 0;
}

bool SdCardFs::list(const char* dir, ListFn fn, void* ctx) {
  if (!mounted_) return false;
  char full[64];
  fullPath(dir, full, sizeof(full));
  DIR* d = opendir(full);
  if (d == nullptr) return false;
  while (struct dirent* e = readdir(d)) {
    if (e->d_type == DT_REG) fn(e->d_name, ctx);
  }
  closedir(d);
  return true;
}

bool SdCardFs::space(uint64_t& totalBytes, uint64_t& freeBytes) {
  if (!mounted_) return false;
  totalBytes = SD.totalBytes();
  const uint64_t used = SD.usedBytes();
  if (totalBytes == 0) return false;
  freeBytes = used < totalBytes ? totalBytes - used : 0;
  return true;
}

void SdCardFs::setTime(uint32_t utc) {
  // FAT stores local time; the system time zone is UTC (never changed), so files get UTC.
  struct timeval tv = {static_cast<time_t>(utc), 0};
  settimeofday(&tv, nullptr);
}
