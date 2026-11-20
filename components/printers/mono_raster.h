// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
// Same ordered dither as the original drivers. Pack a byte locally rather
// than read/modify/write a PSRAM byte for every black pixel. scale_shift is
// 0 for 600 dpi input and 1 for 300->600 nearest-neighbor expansion.
static inline unsigned mono_pack(uint8_t *out, int width, const uint8_t *src,
                                 int source_width, int bpp, int left, int scale_shift, int y)
{
    static const uint8_t matrix[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    const uint8_t *threshold=matrix[y&3];
    int available=(source_width<<scale_shift)-left;
    if(width>available)width=available;
    unsigned ink=0;
    for(int x=0;x<width;x+=8) {
        unsigned bits=0;
        int count=width-x<8?width-x:8;
        if(bpp==1) {
            for(int k=0;k<count;k++) {
                unsigned gray=src[(x+k+left)>>scale_shift];
                unsigned black=gray<(unsigned)threshold[k&3]*16+8;
                bits|=black<<(7-k);ink+=black;
            }
        } else {
            for(int k=0;k<count;k++) {
                const uint8_t *p=src+((x+k+left)>>scale_shift)*3;
                unsigned gray=(77u*p[0]+150u*p[1]+29u*p[2])>>8;
                unsigned black=gray<(unsigned)threshold[k&3]*16+8;
                bits|=black<<(7-k);ink+=black;
            }
        }
        out[x>>3]=(uint8_t)bits;
    }
    return ink;
}
