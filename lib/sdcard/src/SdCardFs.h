#pragma once

#include "FileSystem.h"

// The microSD slot of the senseBox MCU S2 (plan §4.7.1): own SPI bus (HSPI: CS 10, MOSI 11,
// SCK 12, MISO 13), so SD access never competes with the LoRa radio on the default SPI bus, and
// a power switch (SD_ENABLE, pin 9, LOW = on) used to fully reset a hung card.
//
// File access goes through the POSIX/VFS layer under /sd (fopen/fwrite/stat/readdir), which
// needs no String or File objects. The FAT volume itself is mounted by the Arduino SD library.

class SdCardFs : public FileSystem {
 public:
  bool mount() override;
  void unmount() override;
  bool ensureDir(const char* path) override;
  bool append(const char* path, const char* data, size_t len) override;
  int64_t fileSize(const char* path) override;
  uint32_t fileMtime(const char* path) override;
  bool remove(const char* path) override;
  bool list(const char* dir, ListFn fn, void* ctx) override;
  bool space(uint64_t& totalBytes, uint64_t& freeBytes) override;
  void setTime(uint32_t utc) override;

 private:
  bool mounted_ = false;
};
