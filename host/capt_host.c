// SPDX-License-Identifier: GPL-3.0-or-later
// Deterministic CAPT device simulator, not a claim of physical compatibility.
#include "printer_driver.h"
#include "capt_transport.h"
#include "capt-command.h"
#include "capt-status.h"
#include "hiscoa-common.h"
#include "capt_reference/hiscoa-decompress.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const printer_driver_t canon_lbp2900_driver;
#include "capt_sim.h"
static void lifecycle(int paper, int rgb, int dpi)
{
    fake_t f={.force_full=(unsigned)rgb,.input_scale=600/dpi}; printer_io_t link=io(&f); const printer_driver_t *d=&canon_lbp2900_driver;
    f.crop_width=(d->papers[paper].width-d->margin_left-d->margin_right)*600/2540;
    void *j=d->job_begin(d,&link,dpi,paper); assert(j);
    for(int page=0;page<2;page++) {
        int w=d->papers[paper].width*dpi/2540, h=d->papers[paper].height*dpi/2540, bpp=rgb?3:1;
        uint8_t *line=malloc(w*bpp); assert(line);
        assert(!d->page_begin(j,w,h,bpp));
        for(int y=0;y<h;y++) {
            for(int x=0;x<w*bpp;x++) line[x]=((x/bpp+y)&1)?255:0;
            assert(!d->page_line(j,y,(y%5)?line:NULL));
        }
        free(line); assert(!d->page_end(j));
    }
    assert(!d->job_end(j));
    assert(f.paper_width==(unsigned)(d->papers[paper].width*600/2540));
    assert(f.paper_height==(unsigned)(d->papers[paper].height*600/2540));
    assert(f.paper_code==(paper==1?13:paper==2?3:2));
    assert(f.pages==2 && f.black_end==2 && f.start_print==2 && f.video==2*(((d->papers[paper].height-d->margin_top-d->margin_bottom)*600/2540+69)/70) && f.bytes>1000);
}
static void failures(void)
{
    const printer_driver_t *d=&canon_lbp2900_driver;
    fake_t f={.fail_read=1}; printer_io_t link=io(&f);
    assert(!d->job_begin(d,&link,150,0));
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
static void consecutive_jobs(void)
{
    fake_t f={.strict_session=1,.next_job_id=40};
    printer_io_t link=io(&f); const printer_driver_t *d=&canon_lbp2900_driver;
    for(int id=1;id<=3;id++) {
        void *j=d->job_begin(d,&link,600,0);assert(j);
        assert(!d->page_begin(j,4961,7016,1));
        for(int y=0;y<7016;y++) assert(!d->page_line(j,y,NULL));
        assert(!d->page_end(j));assert(!d->job_end(j));
        assert(f.released_jobs==(unsigned)id && !f.reserved && f.job_id==(unsigned)(40+id));
    }
    // The previous known ID can be stale even though the transport is alive.
    f.reserved=1;
    void *recovered=d->job_begin(d,&link,600,0);assert(recovered);
    assert(!d->page_begin(recovered,4961,7016,1));
    for(int y=0;y<7016;y++)assert(!d->page_line(recovered,y,NULL));
    assert(!d->page_end(recovered));assert(!d->job_end(recovered));
    assert(f.job_id==44 && f.released_jobs==5 && !f.reserved);
    f.refuse_release=1;
    void *j=d->job_begin(d,&link,600,0);assert(j);
    assert(!d->page_begin(j,4961,7016,1));
    for(int y=0;y<7016;y++)assert(!d->page_line(j,y,NULL));
    assert(!d->page_end(j));assert(d->job_end(j)<0);
}
int main(void)
{
    assert(printer_driver_match(0x04a9,0x2676,"")==&canon_lbp2900_driver);
    for (int dpi=300; dpi<=600; dpi+=300) for(int paper=0;paper<4;paper++) for(int rgb=0;rgb<2;rgb++) lifecycle(paper,rgb,dpi);
    failures();
    consecutive_jobs();
    puts("CAPT sessions: 3 consecutive jobs on one device, incrementing IDs 41-43, delayed delivery and rejected ReleaseUnit passed");
    puts("CAPT: PASS: A4/Letter/A5/A6, grayscale/RGB, two pages, full-buffer streaming, fragmented replies, disconnect, malformed reply, incomplete/canceled job");
}
