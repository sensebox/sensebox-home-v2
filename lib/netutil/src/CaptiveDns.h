#pragma once

#include <stddef.h>
#include <stdint.h>

// Minimal DNS answer builder for the service-mode captive portal: every A query is answered with
// the access point's own address, so phones open the web UI by themselves. Other query types get
// an empty NOERROR answer. Pure logic (no sockets), so it is tested on the host.
//
// Used instead of the framework's DNSServer, which runs its own network task (RAM is short).

namespace captivedns {

constexpr size_t kMaxPacket = 512;  // classic DNS over UDP

// Builds the response to `query` in `out`. `ipv4` in network order as bytes a.b.c.d.
// Returns the response length, 0 if the packet is not a standard query to answer.
size_t buildResponse(const uint8_t* query, size_t len, const uint8_t ipv4[4], uint8_t* out, size_t cap);

}  // namespace captivedns
