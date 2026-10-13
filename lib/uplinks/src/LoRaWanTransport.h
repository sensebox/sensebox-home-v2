#pragma once

#include <stddef.h>
#include <stdint.h>

// What an uplink needs from a LoRaWAN stack. The real implementation runs MCCI LMIC in its own
// task (plan §6.0) and talks to it through queues; tests and the log-only build use fakes.

class LoRaWanTransport {
 public:
  virtual ~LoRaWanTransport() = default;

  // True once the device has joined the network.
  virtual bool joined() const = 0;
  // True while a previous uplink is still being sent (incl. RX windows).
  virtual bool busy() const = 0;
  // Queues an unconfirmed uplink. `requestTime` piggybacks a DeviceTimeReq (plan §6.1).
  // Returns false if the frame could not be queued (not joined, busy, too long, budget).
  virtual bool send(uint8_t port, const uint8_t* data, size_t len, bool requestTime) = 0;
};
