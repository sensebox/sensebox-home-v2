#include "Sensor.h"

#include <string.h>

const char* sensorStateName(SensorState s) {
  switch (s) {
    case SensorState::Uninitialized: return "uninitialized";
    case SensorState::Initializing: return "initializing";
    case SensorState::WarmingUp: return "warming_up";
    case SensorState::Ready: return "ready";
    case SensorState::Measuring: return "measuring";
    case SensorState::Error: return "error";
    case SensorState::Disabled: return "disabled";
  }
  return "unknown";
}

const char* sensorErrorName(SensorError e) {
  switch (e) {
    case SensorError::None: return "none";
    case SensorError::NotFound: return "not_found";
    case SensorError::CommError: return "comm_error";
    case SensorError::Timeout: return "timeout";
    case SensorError::CrcError: return "crc_error";
    case SensorError::DeviceFault: return "device_fault";
    case SensorError::InvalidData: return "invalid_data";
    case SensorError::NoDriver: return "no_driver";
  }
  return "unknown";
}

const DriverEntry* findDriver(const DriverEntry* table, size_t count, const char* type) {
  if (table == nullptr || type == nullptr) return nullptr;
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(table[i].type, type) == 0) return &table[i];
  }
  return nullptr;
}
