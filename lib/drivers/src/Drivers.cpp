#include "Drivers.h"

#include "Am2315cSensor.h"
#include "Hdc1080Sensor.h"
#include "Sps30Sensor.h"

namespace drivers {
namespace {

Sensor* createAm2315c() { return new Am2315cSensor(); }
Sensor* createHdc1080() { return new Hdc1080Sensor(); }
Sensor* createSps30() { return new Sps30Sensor(); }

const DriverEntry kDrivers[] = {
    {"am2315c", &kAm2315cDescriptor, createAm2315c},
    {"hdc1080", &kHdc1080Descriptor, createHdc1080},
    {"sps30", &kSps30Descriptor, createSps30},
};

}  // namespace

const DriverEntry* table() { return kDrivers; }
size_t count() { return sizeof(kDrivers) / sizeof(kDrivers[0]); }

}  // namespace drivers
