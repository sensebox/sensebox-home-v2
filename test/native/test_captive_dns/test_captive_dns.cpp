#include <string.h>
#include <unity.h>

#include "CaptiveDns.h"

namespace {
const uint8_t kIp[4] = {192, 168, 4, 1};

// Query for "connectivitycheck.gstatic.com", type A, class IN, id 0x1234, RD set.
size_t makeQuery(uint8_t* q, uint16_t type) {
  const uint8_t header[] = {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  size_t n = 0;
  memcpy(q, header, sizeof(header));
  n += sizeof(header);
  const char* labels[] = {"connectivitycheck", "gstatic", "com"};
  for (const char* l : labels) {
    q[n++] = static_cast<uint8_t>(strlen(l));
    memcpy(q + n, l, strlen(l));
    n += strlen(l);
  }
  q[n++] = 0;
  q[n++] = static_cast<uint8_t>(type >> 8);
  q[n++] = static_cast<uint8_t>(type);
  q[n++] = 0;
  q[n++] = 1;
  return n;
}
}  // namespace

void setUp() {}
void tearDown() {}

void test_a_query_is_answered_with_ap_address() {
  uint8_t q[128], r[128];
  const size_t qlen = makeQuery(q, 1);
  const size_t rlen = captivedns::buildResponse(q, qlen, kIp, r, sizeof(r));
  TEST_ASSERT_EQUAL(qlen + 16, rlen);
  TEST_ASSERT_EQUAL_HEX8(0x12, r[0]);  // same id
  TEST_ASSERT_EQUAL_HEX8(0x34, r[1]);
  TEST_ASSERT_TRUE(r[2] & 0x80);       // response
  TEST_ASSERT_EQUAL(1, r[7]);          // one answer
  TEST_ASSERT_EQUAL_MEMORY(q + 12, r + 12, qlen - 12);  // question echoed
  const uint8_t answer[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 192, 168, 4, 1};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(answer, r + qlen, sizeof(answer));
}

void test_aaaa_query_gets_empty_answer() {
  uint8_t q[128], r[128];
  const size_t qlen = makeQuery(q, 28);
  const size_t rlen = captivedns::buildResponse(q, qlen, kIp, r, sizeof(r));
  TEST_ASSERT_EQUAL(qlen, rlen);
  TEST_ASSERT_EQUAL(0, r[7]);  // no answers
  TEST_ASSERT_EQUAL(0, r[3] & 0x0F);  // NOERROR
}

void test_invalid_packets_are_ignored() {
  uint8_t q[128], r[128];
  const size_t qlen = makeQuery(q, 1);
  TEST_ASSERT_EQUAL(0, captivedns::buildResponse(q, 11, kIp, r, sizeof(r)));         // shorter than a header
  TEST_ASSERT_EQUAL(0, captivedns::buildResponse(q, qlen - 3, kIp, r, sizeof(r)));   // truncated question
  TEST_ASSERT_EQUAL(0, captivedns::buildResponse(q, qlen, kIp, r, qlen));            // output too small
  q[2] |= 0x80;                                                                      // a response, not a query
  TEST_ASSERT_EQUAL(0, captivedns::buildResponse(q, qlen, kIp, r, sizeof(r)));
  q[2] &= 0x7F;
  q[12] = 0xC0;  // compression pointer in a query: refuse
  TEST_ASSERT_EQUAL(0, captivedns::buildResponse(q, qlen, kIp, r, sizeof(r)));
  // label length pointing past the end
  makeQuery(q, 1);
  q[12] = 200;
  TEST_ASSERT_EQUAL(0, captivedns::buildResponse(q, qlen, kIp, r, sizeof(r)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_query_is_answered_with_ap_address);
  RUN_TEST(test_aaaa_query_gets_empty_answer);
  RUN_TEST(test_invalid_packets_are_ignored);
  return UNITY_END();
}
