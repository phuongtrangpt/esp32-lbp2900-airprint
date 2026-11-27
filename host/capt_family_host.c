// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol model, NOT a physical printer compatibility test.
#include "printer_driver.h"
#include "cf_capt_transport.h"
#include "cf_capt-command.h"
#include "cf_hiscoa-common.h"
// Same ABI as the unchanged independent reference decoder. Its translation
// unit uses the original header; avoid mixing two private header sets here.
unsigned hiscoa_decompress_band(const void **band, size_t *size, void *output,
    size_t *output_size, unsigned line_size, const struct cf_hiscoa_params *params);
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
extern const printer_driver_t canon_lbp6000_driver, canon_lbp6200_driver,
    canon_lbp3010_driver, canon_lbp3100_driver;
static uint64_t virtual_ms;
int __wrap_clock_gettime(clockid_t id, struct timespec *t)
{ (void)id; t->tv_sec=virtual_ms/1000; t->tv_nsec=(virtual_ms%1000)*1000000; return 0; }
int __wrap_nanosleep(const struct timespec *t, struct timespec *r)
{ (void)r; virtual_ms+=(uint64_t)t->tv_sec*1000+t->tv_nsec/1000000; return 0; }
typedef struct {
    uint8_t reply[512]; size_t len, pos;
    unsigned pages, printed, completed, received, job, next_job, released;
    unsigned setup6000, data, ended, width, height, left, top, row, crop, scale;
    unsigned page_code, expected_code, busy, full, reads, cancel_after;
    unsigned short_status, bad_header, short_reserve, reject_reserve, reject_release;
    unsigned disconnected, stale, paper_empty, paper_reports;
    bool reserved, canceled, restart, blank;
} fake_t;
static unsigned le(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
static void put(uint8_t *p,unsigned n) { p[0]=n;p[1]=n>>8; }
static int write_device(void *ctx,const uint8_t *d,size_t n)
{
    fake_t *f=ctx;
    if(f->disconnected || f->pos!=f->len) return -1;
    assert(n>=4 && le(d+2)==n); unsigned cmd=le(d); const uint8_t *p=d+4;
    f->pos=f->len=0;
    if(cmd==CAPT_SET_PARMS) {
        assert(f->reserved && n==68 && le(p)==CAPT_SET_PARM_PAGE && le(p+2)==44);
        f->page_code=p[8];assert(f->page_code==f->expected_code);
        f->top=le(p+26);f->left=le(p+28);f->width=le(p+30);f->height=le(p+32);
        assert(f->width==(f->crop+7)/8 && f->width<=610 && f->height>0);
        assert(le(p+44)==CAPT_SET_PARM_HISCOA && le(p+46)==12);
        f->pages++;f->row=0;return 0;
    }
    if(cmd==CAPT_PRINT_DATA) {
        uint8_t output[610*70]; size_t output_n=sizeof(output), compressed_n=n-4;
        const void *compressed=p;
        unsigned code=hiscoa_decompress_band(&compressed,&compressed_n,output,&output_n,
            f->width,&hiscoa_default_params);
        unsigned rows=f->height-f->row;if(rows>70)rows=70;
        assert(output_n==rows*f->width && code==(f->row+rows==f->height?1u:0u));
        assert(compressed_n==0 || compressed_n==4);
        for(size_t k=0;k<compressed_n;k++)assert(((const uint8_t *)compressed)[k]==0xbc);
        for(unsigned y=0;y<rows;y++)for(unsigned byte=0;byte<f->width;byte++) {
            unsigned sy=(f->top+f->row+y)/f->scale;
            unsigned sx=(byte*8+f->left)/f->scale;
            unsigned valid=f->crop>byte*8?f->crop-byte*8:0;
            if(valid>8)valid=8;
            // Input is an alternating black/white row. Check every output byte
            // against its closed-form 1x/2x expected bitmap and padded edge.
            uint8_t expected=0;
            if(!f->blank && sy%5 && valid) {
                assert(f->scale==1 || !(f->left&1));
                expected=f->scale==1?(((sx+sy)&1)?0x55:0xaa):(((sx+sy)&1)?0x33:0xcc);
                expected&=(uint8_t)(0xffu<<(8-valid));
            }
            assert(output[y*f->width+byte]==expected);
        }
        f->row+=rows;f->data++;return 0;
    }
    if(cmd==CAPT_PRINT_DATA_END) { assert(f->row==f->height);f->ended++;f->received=f->pages;return 0; }
    size_t payload=2;
    if(cmd==CAPT_CHKXSTATUS)payload=f->short_status?f->short_status:40;
    if(cmd==CAPT_JOB_BEGIN)payload=f->short_reserve?2:4;
    memset(f->reply,0,sizeof(f->reply));put(f->reply,cmd+(f->bad_header?1:0));put(f->reply+2,payload+4);
    uint8_t *s=f->reply+4;
    assert(cmd!=CAPT_CHKSTATUS); // all new families must use extended status
    if(cmd==CAPT_JOB_BEGIN) {
        if(f->reject_reserve || f->reserved)put(s,0x8c);
        else { f->reserved=true;f->job=++f->next_job;f->pages=f->printed=f->completed=f->received=0; }
        if(payload>=4)put(s+2,f->job);
    }
    if(cmd==CAPT_LBP6000_SETUP_0) { assert(n==6 && le(p)==1);f->setup6000++; }
    if(cmd==CAPT_JOB_SETUP) { assert(n==76 && le(p+18)==f->job); }
    if(cmd==CAPT_FIRE) { assert(n==6 && le(p)==f->pages);f->printed=f->completed=f->pages; }
    if(cmd==CAPT_JOB_END) {
        assert(n==6 && le(p)==f->job);
        if(f->reject_release)put(s,0x8c);
        else { assert(f->reserved);f->reserved=false;f->released++; }
    }
    if(cmd==CAPT_CHKXSTATUS && payload>=40) {
        if(f->busy) { s[0]|=0x80;f->busy--; }
        if(f->full) { s[0]|=4;f->full--; }
        if(f->paper_empty)s[0]|=2;
        if(!f->stale) { put(s+14,f->pages);put(s+16,f->printed);put(s+18,f->printed);
            put(s+20,f->completed);put(s+34,f->received); }
    }
    f->len=payload+4;return 0;
}
static int read_device(void *ctx,uint8_t *b,size_t n,unsigned timeout)
{
    fake_t *f=ctx; assert(timeout<=15000);f->reads++;
    if(f->disconnected)return -1;
    if(f->cancel_after && f->reads>=f->cancel_after)f->canceled=true;
    assert(f->pos<f->len);if(n>3)n=3;if(n>f->len-f->pos)n=f->len-f->pos;
    memcpy(b,f->reply+f->pos,n);f->pos+=n;return n;
}
static int flush_device(void *ctx) { return ((fake_t *)ctx)->disconnected?-1:0; }
static bool canceled(void *ctx) { return ((fake_t *)ctx)->canceled; }
static void restart(void *ctx,bool b) { ((fake_t *)ctx)->restart=b; }
static void paper(void *ctx,bool b) { if(b)((fake_t *)ctx)->paper_reports++; }
static printer_io_t io(fake_t *f) { return (printer_io_t){.ctx=f,.write=write_device,.read=read_device,.flush=flush_device,.canceled=canceled,.restart_required=restart,.paper_out=paper}; }
static fake_t device(const printer_driver_t *d,int dpi,int paper_index)
{
    return (fake_t){.next_job=40,.scale=600/dpi,.crop=(d->papers[paper_index].width-d->margin_left-d->margin_right)*600/2540,
        .expected_code=paper_index==1?13:paper_index==2?3:2};
}
static void page(const printer_driver_t *d,void *j,int dpi,int paper_index,int rgb)
{
    int w=d->papers[paper_index].width*dpi/2540,h=d->papers[paper_index].height*dpi/2540,bpp=rgb?3:1;
    assert(!d->page_begin(j,w,h,bpp));uint8_t *row=malloc(w*bpp);assert(row);
    for(int y=0;y<h;y++) {
        for(int x=0;x<w*bpp;x++)row[x]=((x/bpp+y)&1)?255:0;
        assert(!d->page_line(j,y,y%5?row:NULL));
    }
    free(row);assert(!d->page_end(j));
}
static void lifecycle(const printer_driver_t *d,int dpi,int paper_index,int rgb)
{
    fake_t f=device(d,dpi,paper_index); f.busy=2;f.full=2;printer_io_t link=io(&f);
    for(int job=0;job<3;job++) {
        void *j=d->job_begin(d,&link,dpi,paper_index);assert(j);
        for(int p=0;p<2;p++)page(d,j,dpi,paper_index,rgb);
        assert(!d->job_end(j));assert(f.released==(unsigned)job+1 && !f.reserved && !f.restart);
        assert(f.pages==2 && f.printed==2 && f.completed==2);
    }
    assert(f.job==43);
    assert(f.setup6000==((d->usb_pid==0x271a || d->usb_pid==0x271b)?3u:0u));
}
static void failures(void)
{
    const printer_driver_t *d=&canon_lbp6000_driver;
    for(unsigned size=2;size<40;size+=2) {
        fake_t f=device(d,300,0);f.short_status=size;printer_io_t link=io(&f);
        void *j=d->job_begin(d,&link,300,0);assert(j);
        assert(d->page_begin(j,2480,3507,1)<0);assert(d->job_end(j)<0);
    }
    for(int fault=0;fault<4;fault++) {
        fake_t f=device(d,300,0);printer_io_t link=io(&f);
        if(fault==0)f.bad_header=1;
        if(fault==1)f.reject_reserve=1;
        if(fault==2)f.disconnected=1;
        if(fault==3)f.canceled=true;
        void *j=d->job_begin(d,&link,300,0);assert(j);
        assert(d->page_begin(j,2480,3507,1)<0);assert(d->job_end(j)<0);
    }
    // A cancellation arriving inside a fragmented reply must finish that
    // frame before cleanup; otherwise leftover USB bytes corrupt the release.
    fake_t interrupted=device(d,300,0);interrupted.cancel_after=30;
    printer_io_t interrupted_link=io(&interrupted);
    void *interrupted_job=d->job_begin(d,&interrupted_link,300,0);assert(interrupted_job);
    assert(d->page_begin(interrupted_job,2480,3507,1)<0);
    assert(!d->job_abort(interrupted_job) && interrupted.released==1 && !interrupted.restart);
    fake_t f=device(d,300,0);printer_io_t link=io(&f);
    void *j=d->job_begin(d,&link,300,0);assert(j && !d->page_begin(j,2480,3507,1));
    f.canceled=true;assert(!d->job_abort(j));assert(f.released==1 && !f.restart);
    f.canceled=false;
    j=d->job_begin(d,&link,300,0);assert(j && !d->page_begin(j,2480,3507,1));
    f.reject_release=1;assert(d->job_abort(j)<0 && f.restart);
    // All-zero counters must time out instead of acknowledging an unprinted page.
    f=device(d,300,0);f.blank=true;link=io(&f);
    j=d->job_begin(d,&link,300,0);assert(j);
    assert(!d->page_begin(j,1,1,1));assert(!d->page_line(j,0,NULL));f.stale=1;
    uint64_t stalled_at=virtual_ms;
    assert(d->page_end(j)<0 && virtual_ms-stalled_at>=120000);
    assert(d->job_end(j)<0);assert(!f.reserved);
    cf_capt_transport_t t={.io=link,.buffer=malloc(65536)};assert(t.buffer);
    cf_capt_bind(&t);uint64_t start=virtual_ms;
    if(!setjmp(t.guard)) { cf_capt_deadline(&t,35);cf_capt_pause(100);assert(!"deadline did not fire"); }
    assert(virtual_ms-start>=35 && virtual_ms-start<=60);free(t.buffer);cf_capt_bind(NULL);
    puts("CAPT family: fragmented USB, short statuses, bad header, disconnect, cancel, rejected reserve/release, deadlines passed");
}
int main(void)
{
    const printer_driver_t *models[]={&canon_lbp6000_driver,&canon_lbp6200_driver,&canon_lbp3010_driver,&canon_lbp3100_driver};
    for(unsigned m=0;m<4;m++) {
#if defined(PRINTSERVER_PROFILE_LBP6000)
        if(m!=0)continue;
#elif defined(PRINTSERVER_PROFILE_LBP6200)
        if(m!=1)continue;
#elif defined(PRINTSERVER_PROFILE_LBP3010)
        if(m<2)continue;
#endif
        for(int dpi=300;dpi<=600;dpi+=300)for(int p=0;p<3;p++)for(int rgb=0;rgb<2;rgb++)lifecycle(models[m],dpi,p,rgb);
    }
    assert(!printer_driver_match(0x04a9,0x2676,"MFG:Canon;MDL:LBP6000;CMD:CAPT,PCL;"));
    assert(!printer_driver_match(0x03f0,0x3e17,"CMD:PCL,POSTSCRIPT;"));
    assert(!printer_driver_match(0xffff,0x271a,"MFG:Canon;MDL:LBP6000;"));
    for(unsigned m=0;m<4;m++) {
        const printer_driver_t *got=printer_driver_match(0x04a9,models[m]->usb_pid,"");
        assert(got==printer_driver_find(models[m]->id));
    }
    failures();
    puts("CAPT family: PASS selected family: 3 consecutive two-page jobs per paper/dpi/color, A4/Letter/A5, 300/600 dpi, gray/RGB; independent Hi-SCoA decode; model-only registry");
}
