#include "CaptiveDns.h"

#include <string.h>

namespace captivedns {
namespace {

constexpr size_t kHeaderLen = 12;
constexpr uint16_t kTypeA = 1;
constexpr uint16_t kClassIn = 1;
constexpr uint32_t kTtlS = 60;

uint16_t read16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

void write16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

}  // namespace

size_t buildResponse(const uint8_t* query, size_t len, const uint8_t ipv4[4], uint8_t* out, size_t cap) {
  if (query == nullptr || len < kHeaderLen || len > kMaxPacket) return 0;
  const uint16_t flags = read16(query + 2);
  const bool isResponse = (flags & 0x8000) != 0;
  const uint8_t opcode = (flags >> 11) & 0x0F;
  if (isResponse || opcode != 0 || read16(query + 4) == 0) return 0;  // only standard queries

  // Walk the first question's name (labels only; compression pointers don't occur in queries).
  size_t pos = kHeaderLen;
  while (pos < len && query[pos] != 0) {
    if ((query[pos] & 0xC0) != 0) return 0;
    pos += 1 + query[pos];
  }
  pos += 1;                      // terminating zero label
  if (pos + 4 > len) return 0;   // QTYPE + QCLASS
  const uint16_t qtype = read16(query + pos);
  const uint16_t qclass = read16(query + pos + 2);
  const size_t questionEnd = pos + 4;

  const bool answerA = qtype == kTypeA && qclass == kClassIn;
  const size_t answerLen = answerA ? 16 : 0;
  if (questionEnd + answerLen > cap) return 0;

  // Header + first question copied from the query, extra questions/records dropped.
  memcpy(out, query, questionEnd);
  const bool recursionDesired = (flags & 0x0100) != 0;
  write16(out + 2, static_cast<uint16_t>(0x8000 | 0x0400 | (recursionDesired ? 0x0100 : 0) | 0x0080));  // QR AA RD RA
  write16(out + 4, 1);                 // QDCOUNT
  write16(out + 6, answerA ? 1 : 0);   // ANCOUNT
  write16(out + 8, 0);                 // NSCOUNT
  write16(out + 10, 0);                // ARCOUNT
  if (!answerA) return questionEnd;

  uint8_t* a = out + questionEnd;
  write16(a, 0xC00C);  // name: pointer to the question name at offset 12
  write16(a + 2, kTypeA);
  write16(a + 4, kClassIn);
  a[6] = 0;
  a[7] = 0;
  write16(a + 8, static_cast<uint16_t>(kTtlS));
  write16(a + 10, 4);
  memcpy(a + 12, ipv4, 4);
  return questionEnd + answerLen;
}

}  // namespace captivedns
