#pragma once

#include <stddef.h>
#include <stdint.h>

// Minimal file system used by SdStore (plan §4.5). Implemented by the SD card driver on the
// device (lib/sdcard) and by an in-memory fake in the host tests.
//
// Paths are absolute from the card root, e.g. "/data/B000042-0001.csv".

class FileSystem {
 public:
  using ListFn = void (*)(const char* name, void* ctx);  // name without directory

  virtual ~FileSystem() = default;

  // Powers the card and mounts it. False if no card is present or it can't be mounted.
  virtual bool mount() = 0;
  // Unmounts and powers the card off (a later mount() starts from a clean power-up).
  virtual void unmount() = 0;

  virtual bool ensureDir(const char* path) = 0;
  // Appends `len` bytes, creating the file if needed. False on any write error.
  virtual bool append(const char* path, const char* data, size_t len) = 0;
  // Size in bytes, or -1 if the file doesn't exist.
  virtual int64_t fileSize(const char* path) = 0;
  // Last modification time (UTC seconds), 0 if unknown (e.g. written before the clock was set).
  virtual uint32_t fileMtime(const char* path) = 0;
  virtual bool remove(const char* path) = 0;
  // Calls `fn` for every regular file in `dir` (names only: fast, no per-file metadata reads).
  virtual bool list(const char* dir, ListFn fn, void* ctx) = 0;
  virtual bool space(uint64_t& totalBytes, uint64_t& freeBytes) = 0;

  // Sets the time used for file timestamps (the system clock on the device).
  virtual void setTime(uint32_t utc) { (void)utc; }
};
