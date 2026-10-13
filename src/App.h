#pragma once

#include "Clock.h"
#include "Health.h"
#include "LmicTransport.h"
#include "SensorManager.h"
#include "StationConfig.h"

// Objects the serial console (and later the REST API) needs to inspect. Owned by main.cpp.
struct App {
  Clock& clock;
  HealthRegistry& health;
  config::StationConfig& config;
  SensorManager& sensors;
  LmicTransport& lora;
  bool safeMode;
};
