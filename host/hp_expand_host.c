#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../components/printers/hp_p1006_expand.h"
int main(void) {
    uint8_t input[660], actual[1320], expected[1320];
    for (unsigned width=1;width<=5200;++width) {
        for (unsigned v=0;v<256;++v) {
            memset(input,v,sizeof(input));
            memset(actual,0,sizeof(actual)); memset(expected,0,sizeof(expected));
            for (unsigned x=0;x<width;++x)
                if(input[x/8] & (0x80>>(x%8))) expected[x/4] |= 0xc0>>((x%4)*2);
            hp_p1006_expand_row(actual,input,width);
            assert(!memcmp(actual,expected,sizeof(actual)));
        }
    }
    puts("P1006 packing: all 256 byte values across widths 1..5200, padding and tail bits match pixel reference");
}
