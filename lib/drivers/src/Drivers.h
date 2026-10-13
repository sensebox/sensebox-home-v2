#pragma once

#include <stddef.h>

#include "Sensor.h"

// The driver registry: every sensor type this firmware supports.
// Adding a sensor = adding a driver class and one line in Drivers.cpp.

namespace drivers {

const DriverEntry* table();
size_t count();

}  // namespace drivers
