#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Airtime.h"
#include "Clock.h"
#include "Health.h"
#include "LoRaWanTransport.h"
#include "StationConfig.h"

// MCCI LMIC behind the LoRaWanTransport interface (plan §6.0).
//
// LMIC runs in its own FreeRTOS task, which is the only place that calls LMIC_* / os_*.
// The main loop talks to it through two queues: commands in (send), events out (joined,
// TX complete, network time, ...). poll() applies the events in the main task, so the Clock
// and the HealthRegistry are only ever touched from there.

class LmicTransport : public LoRaWanTransport {
 public:
  LmicTransport(Clock& clock, HealthRegistry& health);

  // `appKey` is the 16-byte AppKey (MSB first); EUIs come from the config (MSB-first hex).
  // Returns false if keys are missing/invalid or the task can't be started.
  bool begin(const config::LoRaWanConfig& cfg, const uint8_t appKey[16]);

  // Call from the main loop: applies events from the LMIC task, supervises the radio.
  void poll(uint32_t nowMs);

  bool joined() const override { return joined_; }
  bool busy() const override { return busy_; }
  bool send(uint8_t port, const uint8_t* data, size_t len, bool requestTime) override;

  struct Status {
    bool joined;
    uint32_t joinAttempts;
    uint32_t uplinks;
    uint32_t downlinks;
    int16_t lastRssi;
    int8_t lastSnr;
    uint8_t spreadingFactor;  // of the last uplink, 0 = unknown
    int32_t budgetMs;         // remaining fair-use airtime
  };
  Status status(uint32_t nowMs);

 private:
  void handleEvent(uint32_t nowMs);

  Clock& clock_;
  HealthRegistry& health_;
  HealthRegistry::Id healthId_;
  airtime::Budget budget_;
  bool started_ = false;
  bool joined_ = false;
  bool busy_ = false;
  uint32_t busySinceMs_ = 0;
  uint32_t joinAttempts_ = 0;
  uint32_t uplinks_ = 0;
  uint32_t downlinks_ = 0;
  int16_t lastRssi_ = 0;
  int8_t lastSnr_ = 0;
  uint8_t lastSf_ = 0;
  uint8_t radioResets_ = 0;
  uint8_t pendingLen_ = 0;       // payload length of the uplink in flight (for the budget)
  bool pendingTimeReq_ = false;
};
