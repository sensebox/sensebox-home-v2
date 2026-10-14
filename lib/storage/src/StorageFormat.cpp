#include "StorageFormat.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace storagefmt {
namespace {

// Days since 1970-01-01 -> civil date (Howard Hinnant's algorithm, proleptic Gregorian).
void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<int>(yoe) + static_cast<int>(era) * 400 + (m <= 2 ? 1 : 0);
}

size_t clampLen(int n, size_t cap) {
  if (n < 0) return 0;
  return static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;  // 0 = didn't fit
}

}  // namespace

size_t formatIsoUtc(uint32_t utc, char* buf, size_t cap) {
  int y;
  unsigned m, d;
  civilFromDays(utc / 86400, y, m, d);
  const uint32_t s = utc % 86400;
  return clampLen(snprintf(buf, cap, "%04d-%02u-%02uT%02lu:%02lu:%02luZ", y, m, d, static_cast<unsigned long>(s / 3600),
                           static_cast<unsigned long>(s / 60 % 60), static_cast<unsigned long>(s % 60)),
                  cap);
}

size_t formatMeasurement(const Measurement& m, char* buf, size_t cap) {
  char time[24] = "";
  char uptime[12] = "";
  if (m.quality & quality::kTimeUnsynced) {
    snprintf(uptime, sizeof(uptime), "%lu", static_cast<unsigned long>(m.timestamp));
  } else {
    formatIsoUtc(m.timestamp, time, sizeof(time));
  }
  char value[16];
  if (isnan(m.value)) {
    strcpy(value, "nan");
  } else {
    snprintf(value, sizeof(value), "%.2f", static_cast<double>(m.value));
  }
  return clampLen(snprintf(buf, cap, "%s,%s,%u,%s,%s,%s,%s,0x%03x\n", time, uptime, m.sensorSlot,
                           phenomenonName(m.phenomenon), valueKindName(m.kind), value, unitSymbol(m.unit), m.quality),
                  cap);
}

size_t formatLogLine(const LogLine& line, uint32_t utc, char* buf, size_t cap) {
  char time[24] = "-";
  if (utc != 0) formatIsoUtc(utc, time, sizeof(time));
  return clampLen(snprintf(buf, cap, "%-20s [%7lu] %c %-8s %s\n", time, static_cast<unsigned long>(line.uptimeS),
                           logger::levelLetter(line.level), line.tag, line.msg),
                  cap);
}

size_t formatFileHeader(const char* firmware, const char* station, uint32_t bootId, char* buf, size_t cap) {
  return clampLen(snprintf(buf, cap, "# senseBox:home v2 firmware %s, station \"%s\", boot %lu\n", firmware, station,
                           static_cast<unsigned long>(bootId)),
                  cap);
}

size_t formatSyncMarker(uint32_t uptimeS, uint32_t utc, uint32_t bootId, char* buf, size_t cap) {
  char time[24];
  formatIsoUtc(utc, time, sizeof(time));
  return clampLen(snprintf(buf, cap, "# time synced: uptime_s %lu of boot %lu = %s\n", static_cast<unsigned long>(uptimeS),
                           static_cast<unsigned long>(bootId), time),
                  cap);
}

void filePath(const char* dir, uint32_t bootId, uint16_t part, const char* ext, char* buf, size_t cap) {
  snprintf(buf, cap, "%s/B%06lu-%04u.%s", dir, static_cast<unsigned long>(bootId % 1000000u), part, ext);
}

bool parseFileName(const char* name, uint32_t& bootId, uint16_t& part) {
  // B + 6 digits + '-' + 4 digits + '.' + extension
  if (name == nullptr || name[0] != 'B' || strlen(name) < 13 || name[7] != '-' || name[12] != '.') return false;
  uint32_t boot = 0;
  for (int i = 1; i <= 6; ++i) {
    if (!isdigit(static_cast<unsigned char>(name[i]))) return false;
    boot = boot * 10 + static_cast<uint32_t>(name[i] - '0');
  }
  uint32_t p = 0;
  for (int i = 8; i <= 11; ++i) {
    if (!isdigit(static_cast<unsigned char>(name[i]))) return false;
    p = p * 10 + static_cast<uint32_t>(name[i] - '0');
  }
  bootId = boot;
  part = static_cast<uint16_t>(p);
  return true;
}

}  // namespace storagefmt
