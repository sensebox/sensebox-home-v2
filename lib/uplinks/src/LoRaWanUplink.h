#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Aggregator.h"
#include "CayenneEncoder.h"
#include "Clock.h"
#include "CompactEncoder.h"
#include "Health.h"
#include "LoRaWanTransport.h"
#include "MeasurementSink.h"
#include "StationConfig.h"

// The LoRaWAN data targets (plan §6), "lorawan-cayenne" and "lorawan-compact":
// collects measurements, builds window means every `intervalSec`, encodes them (Cayenne LPP on
// port 1, or the compact layout with the window end time on port 2, see CompactEncoder.h) and
// hands the frame to the LoRaWAN transport.
//
// Once the clock is synced, windows end on the UTC grid (600 s -> XX:00, XX:10, ...). The window
// is closed a few seconds after the boundary, so the measurement taken exactly on it is still
// included: a window is (XX:00, XX:10]. The frame is sent with a fixed per-box delay derived from
// the DevEUI, so the boxes of a deployment don't all transmit in the same second and collide at
// the gateway.
//
// No catch-up: the openSenseMap stamps the values with the arrival time, so a window that can't
// be sent is dropped (it is on the SD card), plan §6.2.

class LoRaWanUplink : public MeasurementSink {
 public:
  // Grace period after the window end for the measurement taken on the boundary.
  static constexpr uint32_t kCloseDelayMs = 5000;
  // Upper bound of the per-box send delay (also capped at a quarter of the interval).
  static constexpr uint32_t kMaxSendDelayMs = 120000;

  LoRaWanUplink(const config::StationConfig& station, config::UplinkType type, const config::LoRaWanConfig& cfg,
                Clock& clock, HealthRegistry& health, LoRaWanTransport& transport);

  uint8_t port() const { return type_ == config::UplinkType::LoRaWanCompact ? compact::kPort : cayenne::kPort; }

  // Optional consumer of the window means (e.g. SD storage).
  void setAggregateSink(MeasurementSink* sink) { aggregateSink_ = sink; }
  // Delay between closing a window and sending it. Default: derived from the DevEUI.
  void setSendDelayMs(uint32_t ms) { sendDelayMs_ = ms; }
  uint32_t sendDelayMs() const { return sendDelayMs_; }

  void begin(uint32_t nowMs);
  void tick(uint32_t nowMs);
  void onMeasurement(const Measurement& m) override;

  struct Stats {
    uint32_t windows = 0;      // windows closed
    uint32_t sent = 0;         // frames handed to the transport
    uint32_t skipped = 0;      // windows not sent (not joined / busy / empty / replaced)
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
  bool onGrid() const;
  void alignWindow(uint32_t nowMs, bool mergeShort);
  void closeWindow(uint32_t nowMs);
  size_t encodeFrame(uint32_t ts, bool utc, uint8_t* out, size_t cap);
  void sendPending();

  const config::StationConfig& station_;
  const config::UplinkType type_;
  const config::LoRaWanConfig& cfg_;
  Clock& clock_;
  HealthRegistry& health_;
  LoRaWanTransport& transport_;
  MeasurementSink* aggregateSink_ = nullptr;
  HealthRegistry::Id healthId_;

  Aggregator aggregator_;
  WindowCollector window_;
  uint32_t windowEndMs_ = 0;   // when the current window is closed (incl. kCloseDelayMs on the grid)
  uint32_t windowEndUtc_ = 0;  // UTC end of the current window, 0 = not on the grid
  uint32_t gridSync_ = 0;      // Clock::syncCount() the window was aligned to
  bool started_ = false;
  uint32_t sendDelayMs_ = 0;
  uint8_t pending_[cayenne::kMaxPayload] = {};
  uint8_t pendingLen_ = 0;
  uint32_t sendAtMs_ = 0;
  Stats stats_;
};
