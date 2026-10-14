#pragma once

#include "App.h"
#include "LatestValues.h"
#include "StatusSnapshot.h"

// Fills `out` with the current status of normal operation (health, sensors, latest values,
// LoRaWAN, recent log lines) and seals it. Used right before restarting into service mode.
void captureStatus(const App& app, const LatestValues& latest, uint32_t nowMs, StatusSnapshot& out);
