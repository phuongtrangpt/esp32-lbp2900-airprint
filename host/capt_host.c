// SPDX-License-Identifier: GPL-3.0-or-later
// Deterministic CAPT device simulator, not a claim of physical compatibility.
#include "printer_driver.h"
#include "capt_transport.h"
#include "capt-command.h"
#include "hiscoa-common.h"
#include "capt_reference/hiscoa-decompress.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const printer_driver_t canon_lbp2900_driver;
#include "capt_sim.h"
static void lifecycle(int paper, int rgb)
{
    fake_t f={.force_full=(unsigned)rgb}; printer_io_t link=io(&f); const printer_driver_t *d=&canon_lbp2900_driver;
    void *j=d->job_begin(d,&link,600,paper); assert(j);
    for(int page=0;page<2;page++) {
        int w=paper?5100:4960, h=paper?6600:7015, bpp=rgb?3:1;
        uint8_t *line=malloc(w*bpp); assert(line);
        assert(!d->page_begin(j,w,h,bpp));
        for(int y=0;y<h;y++) {
            for(int x=0;x<w*bpp;x++) line[x]=((x/bpp+y)&1)?255:0;
            assert(!d->page_line(j,y,(y%5)?line:NULL));
        }
        free(line); assert(!d->page_end(j));
    }
    assert(!d->job_end(j));
    assert(f.pages==2 && f.black_end==2 && f.start_print==2 && f.video>100 && f.bytes>1000);
}
static void failures(void)
{
    const printer_driver_t *d=&canon_lbp2900_driver;
    fake_t f={.fail_read=1}; printer_io_t link=io(&f);
    assert(!d->job_begin(d,&link,300,0));
    assert(!d->job_begin(d,&link,600,9));
    void *j=d->job_begin(d,&link,600,0); assert(j);
    assert(d->page_begin(j,4960,7015,1)<0); assert(d->job_end(j)<0);
    memset(&f,0,sizeof(f)); f.bad_cmd=1;
    j=d->job_begin(d,&link,600,0); assert(j);
    assert(d->page_begin(j,4960,7015,1)<0); assert(d->job_end(j)<0);
    memset(&f,0,sizeof(f));
    j=d->job_begin(d,&link,600,0); assert(j);
    assert(!d->page_begin(j,4960,7015,1));
    assert(d->page_line(j,1,NULL)<0); // no out-of-order line accepted
    assert(d->job_end(j)<0); // unfinished page triggers cleanup
    memset(&f,0,sizeof(f));
    j=d->job_begin(d,&link,600,0); assert(j);
    assert(!d->page_begin(j,4960,7015,1));
    f.fail_read=1;
    int failed=0;
    for(int y=0;y<7015;y++) if(d->page_line(j,y,NULL)<0) { failed=1;break; }
    assert(failed); assert(d->job_end(j)<0);
}
int main(void)
{
    assert(printer_driver_match(0x04a9,0x2676,"")==&canon_lbp2900_driver);
    lifecycle(0,0); lifecycle(1,1); failures();
    puts("CAPT: PASS: A4/Letter, grayscale/RGB, two pages, full-buffer streaming, fragmented replies, disconnect, malformed reply, incomplete/canceled job");
}
