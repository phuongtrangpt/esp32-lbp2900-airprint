// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Track record boundaries without retaining the print payload. Unknown
// streams can still use ordinary TCP EOF, but cannot recover from a reset.
typedef struct {
    enum { XQX_PREAMBLE, XQX_RECORD, XQX_ITEM, XQX_END, XQX_UNKNOWN } state;
    enum { XQX_DOCUMENT_START, XQX_BETWEEN_PAGES, XQX_PAGE,
           XQX_PLANE_START, XQX_PLANE_END, XQX_PAGE_END } phase;
    uint8_t header[8];
    size_t used;
    uint32_t skip, items;
    unsigned preamble_bytes, marker_bytes, trailer_bytes;
    unsigned pages_done;
} hp_raw_xqx_t;

static const char hp_raw_xqx_trailer[] = "\033%-12345X@PJL EOJ\n\033%-12345X";

static inline uint32_t hp_raw_xqx_u32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static inline void hp_raw_xqx_record(hp_raw_xqx_t *x, uint32_t type, uint32_t count)
{
    switch (type) {
    case 1: // document header and attributes
        if (x->phase != XQX_DOCUMENT_START || !count) break;
        x->phase = XQX_BETWEEN_PAGES;
        goto items;
    case 3: // page header and attributes
        if (x->phase != XQX_BETWEEN_PAGES || !count) break;
        x->phase = XQX_PAGE;
        goto items;
    case 5: // JBIG block attributes
        if ((x->phase != XQX_PAGE && x->phase != XQX_PAGE_END) || !count) break;
        x->phase = XQX_PLANE_START;
        goto items;
    case 7: // JBIG bytes
        if (x->phase != XQX_PLANE_START || !count) break;
        x->phase = XQX_PLANE_END;
        x->skip = count;
        return;
    case 6: // end of block
        if (x->phase != XQX_PLANE_END || count) break;
        x->phase = XQX_PAGE_END;
        return;
    case 4: // end of page
        if (x->phase != XQX_PAGE_END || count) break;
        x->phase = XQX_BETWEEN_PAGES;
        ++x->pages_done;
        return;
    case 2: // end of document
        if (x->phase != XQX_BETWEEN_PAGES || !x->pages_done || count) break;
        x->state = XQX_END;
        return;
    }
    x->state = XQX_UNKNOWN;
    return;

items:
    x->items = count;
    x->state = XQX_ITEM;
}

static inline void hp_raw_xqx_feed(hp_raw_xqx_t *x, const uint8_t *p, size_t len)
{
    static const char marker[] = "\033%-12345X,XQX";
    while (len && x->state != XQX_UNKNOWN) {
        if (x->skip) {
            size_t n = x->skip < len ? x->skip : len;
            x->skip -= n;
            p += n;
            len -= n;
            continue;
        }
        uint8_t b = *p++;
        --len;
        if (x->state == XQX_PREAMBLE) {
            if (b == marker[x->marker_bytes]) {
                if (++x->marker_bytes == sizeof(marker) - 1)
                    x->state = XQX_RECORD;
            } else {
                x->marker_bytes = b == marker[0];
            }
            if (++x->preamble_bytes > 4096)
                x->state = XQX_UNKNOWN;
        } else if (x->state == XQX_END) {
            if (x->trailer_bytes >= sizeof(hp_raw_xqx_trailer) - 1 ||
                b != hp_raw_xqx_trailer[x->trailer_bytes++])
                x->state = XQX_UNKNOWN;
        } else {
            x->header[x->used++] = b;
            if (x->used != sizeof(x->header))
                continue;
            uint32_t count = hp_raw_xqx_u32(x->header + 4);
            x->used = 0;
            if (x->state == XQX_ITEM) {
                x->skip = count;
                if (--x->items == 0)
                    x->state = XQX_RECORD;
            } else {
                hp_raw_xqx_record(x, hp_raw_xqx_u32(x->header), count);
            }
        }
    }
}

static inline bool hp_raw_xqx_complete(const hp_raw_xqx_t *x)
{
    return x->state == XQX_END &&
           (x->trailer_bytes == 0 || x->trailer_bytes == sizeof(hp_raw_xqx_trailer) - 1);
}
