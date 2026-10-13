#pragma once

#include <stdint.h>

// Minimal mutex with bounded waits. FreeRTOS on the device, a no-op on the host.
// Never use from an ISR.

#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Mutex {
 public:
  Mutex() : handle_(xSemaphoreCreateMutexStatic(&buffer_)) {}
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;

  bool lock(uint32_t timeoutMs) { return xSemaphoreTake(handle_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE; }
  void unlock() { xSemaphoreGive(handle_); }

 private:
  StaticSemaphore_t buffer_;
  SemaphoreHandle_t handle_;
};

#else

class Mutex {
 public:
  bool lock(uint32_t /*timeoutMs*/) { return true; }
  void unlock() {}
};

#endif

// Scoped lock. Check locked() before touching shared state.
class MutexLock {
 public:
  MutexLock(Mutex& mutex, uint32_t timeoutMs) : mutex_(mutex), locked_(mutex.lock(timeoutMs)) {}
  ~MutexLock() {
    if (locked_) mutex_.unlock();
  }
  MutexLock(const MutexLock&) = delete;
  MutexLock& operator=(const MutexLock&) = delete;

  bool locked() const { return locked_; }

 private:
  Mutex& mutex_;
  bool locked_;
};
