#include "portal_dns.h"
#include <string.h>
size_t portal_dns_reply(const uint8_t *q, size_t n, uint8_t *o, size_t cap,
                        const uint8_t ip[4]) {
  if (!q || !o || !ip || n < 17 || n > 512 || (q[2] & 0xf8) || q[4] ||
      q[5] != 1)
    return 0;
  size_t at = 12, labels = 0;
  while (at < n && q[at]) {
    unsigned len = q[at++];
    if (len > 63 || at + len >= n || at + len - 12 > 254 || ++labels > 127)
      return 0;
    at += len;
  }
  if (at >= n || at + 5 > n)
    return 0;
  at++;
  size_t end = at + 4;
  if (q[at + 2] != 0 || q[at + 3] != 1)
    return 0;
  int a = q[at] == 0 && q[at + 1] == 1;
  if (end + (a ? 16 : 0) > cap)
    return 0;
  memcpy(o, q, end);
  o[2] = 0x81;
  o[3] = 0x80;
  memset(o + 6, 0, 6);
  o[7] = a ? 1 : 0;
  if (a) {
    const uint8_t rr[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4};
    memcpy(o + end, rr, 12);
    memcpy(o + end + 12, ip, 4);
    end += 16;
  }
  return end;
}
