#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Log.h"
#include "Measurement.h"

// Text formats and file names on the SD card (plan §4.5). Pure functions, host-tested.
//
// Files are named by boot number and part, not by date: after a power cut the box has no time
// until the first LoRaWAN time sync, but it must record from the first second.
//   /data/B000042-0001.csv   measurements of boot 42, part 1
//   /logs/B000042-0001.log   log of boot 42, part 1
// Sorting the names sorts the files by age, which is what the janitor deletes by.

namespace storagefmt {

constexpr const char* kDataDir = "/data";
constexpr const char* kLogDir = "/logs";
constexpr size_t kPathLen = 40;
constexpr size_t kMaxLineLen = 160;

// CSV header of measurement files.
constexpr const char* kMeasurementHeader = "utc,uptime_s,slot,phenomenon,kind,value,unit,quality\n";

// "2026-10-14T09:30:00Z". Returns the length (20).
size_t formatIsoUtc(uint32_t utc, char* buf, size_t cap);

// One CSV line (with '\n'). A measurement taken before the first time sync has an empty `utc`
// column and the seconds since boot in `uptime_s`; afterwards it is the other way round.
size_t formatMeasurement(const Measurement& m, char* buf, size_t cap);

// One log line (with '\n'): "2026-10-14T09:30:00Z [    123] I tag      message", with "-" instead
// of the date while the clock is not synced (utc = 0).
size_t formatLogLine(const LogLine& line, uint32_t utc, char* buf, size_t cap);

// First lines of a new file.
size_t formatFileHeader(const char* firmware, const char* station, uint32_t bootId, char* buf, size_t cap);

// Comment line written into the measurement file when the clock is synced for the first time in a
// boot. It allows converting the earlier uptime_s values to UTC afterwards.
size_t formatSyncMarker(uint32_t uptimeS, uint32_t utc, uint32_t bootId, char* buf, size_t cap);

// "/data/B000042-0001.csv". `ext` without dot.
void filePath(const char* dir, uint32_t bootId, uint16_t part, const char* ext, char* buf, size_t cap);

// Parses "B000042-0001.csv" (name only). Returns false for any other name.
bool parseFileName(const char* name, uint32_t& bootId, uint16_t& part);

// Sort key of a parsed name: older files have smaller keys.
inline uint64_t fileKey(uint32_t bootId, uint16_t part) { return (static_cast<uint64_t>(bootId) << 16) | part; }

}  // namespace storagefmt
