#pragma once

#include "Clock.h"
#include "Health.h"
#include "LatestValues.h"
#include "LmicTransport.h"
#include "SdStore.h"
#include "SelfTest.h"
#include "SensorManager.h"
#include "StationConfig.h"

// Objects the serial console needs to inspect. Owned by main.cpp.
struct App {
  Clock& clock;
  HealthRegistry& health;
  config::StationConfig& config;
  SensorManager& sensors;
  const LatestValues& latest;  // most recent value per sensor and phenomenon
  LmicTransport& lora;
  SdStore& storage;
  const SelfTest& selfTest;
  bool safeMode;
  bool serviceMode;  // sensors and LoRaWAN are not running; see BootMode.h
};
