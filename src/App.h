#pragma once

#include "Clock.h"
#include "Health.h"
#include "LmicTransport.h"
#include "SdStore.h"
#include "SensorManager.h"
#include "StationConfig.h"

// Objects the serial console needs to inspect. Owned by main.cpp.
struct App {
  Clock& clock;
  HealthRegistry& health;
  config::StationConfig& config;
  SensorManager& sensors;
  LmicTransport& lora;
  SdStore& storage;
  bool safeMode;
  bool serviceMode;  // sensors and LoRaWAN are not running; see BootMode.h
};
