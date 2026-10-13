#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Aggregator.h"
#include "CayenneEncoder.h"
#include "Clock.h"
#include "Health.h"
#include "LoRaWanTransport.h"
#include "MeasurementSink.h"
#include "StationConfig.h"

// The "lorawan-cayenne" data target (plan §6):
// collects measurements, builds window means every `intervalSec`, encodes them as Cayenne LPP
// and hands the frame to the LoRaWAN transport.
//
// No catch-up: Cayenne frames carry no timestamp and the openSenseMap stamps them with the
// arrival time, so a window that can't be sent is dropped (it is on the SD card), plan §6.2.

class CayenneUplink : public MeasurementSink {
 public:
  static constexpr uint8_t kPort = 1;

  CayenneUplink(const config::StationConfig& station, const config::LoRaWanConfig& cfg, Clock& clock,
                HealthRegistry& health, LoRaWanTransport& transport);

  // Optional consumer of the window means (e.g. SD storage).
  void setAggregateSink(MeasurementSink* sink) { aggregateSink_ = sink; }

  void begin(uint32_t nowMs);
  void tick(uint32_t nowMs);
  void onMeasurement(const Measurement& m) override;

  struct Stats {
    uint32_t windows = 0;      // windows closed
    uint32_t sent = 0;         // frames handed to the transport
    uint32_t skipped = 0;      // windows not sent (not joined / busy / empty)
    uint32_t clampedValues = 0;
    uint8_t lastLength = 0;
    uint8_t lastFrame[cayenne::kMaxPayload] = {};
  };
  const Stats& stats() const { return stats_; }

 private:
  // Collects the flushed window means so they can be encoded together.
  class WindowCollector : public MeasurementSink {
   public:
    void reset() { count = 0; }
    void onMeasurement(const Measurement& m) override;
    static constexpr size_t kMax = Aggregator::kMaxSeries;
    Measurement items[kMax];
    size_t count = 0;
    MeasurementSink* forward = nullptr;
  };

  bool timeSyncDue() const;
  void closeWindow(uint32_t nowMs);

  const config::StationConfig& station_;
  const config::LoRaWanConfig& cfg_;
  Clock& clock_;
  HealthRegistry& health_;
  LoRaWanTransport& transport_;
  MeasurementSink* aggregateSink_ = nullptr;
  HealthRegistry::Id healthId_;

  Aggregator aggregator_;
  WindowCollector window_;
  uint32_t windowEndMs_ = 0;
  bool started_ = false;
  Stats stats_;
};
