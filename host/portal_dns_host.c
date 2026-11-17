#include "portal_dns.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
  const uint8_t ip[] = {192, 168, 4, 1};
  uint8_t out[528], q[] = {0x12, 0x34, 1,   0, 0,   1,   0,   0, 0, 0, 0, 0, 3,
                           'w',  'w',  'w', 3, 'c', 'o', 'm', 0, 0, 1, 0, 1};
  size_t n = portal_dns_reply(q, sizeof(q), out, sizeof(out), ip);
  assert(n == sizeof(q) + 16 && out[7] == 1 && !memcmp(out + n - 4, ip, 4));
  q[22] = 28;
  n = portal_dns_reply(q, sizeof(q), out, sizeof(out), ip);
  assert(n == sizeof(q) && out[7] == 0);
  q[22] = 1;
  assert(!portal_dns_reply(q, sizeof(q), out, 12, ip));
  q[12] = 0xc0;
  assert(!portal_dns_reply(q, sizeof(q), out, sizeof(out), ip));
  q[12] = 3;
  for (size_t i = 0; i < sizeof(q); i++)
    assert(!portal_dns_reply(q, i, out, sizeof(out), ip));
  uint32_t r = 1234567;
  uint8_t fuzz[513];
  for (int i = 0; i < 10000; i++) {
    size_t len = i % 514;
    for (size_t k = 0; k < len; k++) {
      r = r * 1664525 + 1013904223;
      fuzz[k] = r >> 24;
    }
    portal_dns_reply(fuzz, len, out, sizeof(out), ip);
  }
  puts("Captive DNS: A/AAAA, bounded response, truncated/compressed malformed "
       "queries and 10000 random packets passed");
  return 0;
}
