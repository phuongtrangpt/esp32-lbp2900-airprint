// P1006's exact 1-bit -> 2-bit packing (white=00, black=11).
#pragma once
#include <stdint.h>
static inline void hp_p1006_expand_row(uint8_t *out, const uint8_t *in, unsigned width)
{
    static const uint8_t nibble[16] = {
        0x00,0x03,0x0c,0x0f,0x30,0x33,0x3c,0x3f,
        0xc0,0xc3,0xcc,0xcf,0xf0,0xf3,0xfc,0xff
    };
    unsigned bytes = (width + 7) / 8;
    for (unsigned x = 0; x < bytes; ++x) {
        uint8_t bits = in[x];
        if (x + 1 == bytes && (width & 7))
            bits &= (uint8_t)(0xffu << (8 - (width & 7)));
        if (bits) { // destination stripe was cleared by the caller
            out[2*x] = nibble[bits >> 4];
            out[2*x+1] = nibble[bits & 15];
        }
    }
}
