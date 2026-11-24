// SPDX-License-Identifier: GPL-3.0-or-later
// XQX framing adapted from Rick Richardson's foo2xqx (GPL-2.0-or-later).
// Experimental HP LaserJet P1006, 600 dpi monochrome, simplex A4 / Letter / A5 / A6.
#include "printer_driver.h"
#include "mono_raster.h"
#include "hp_p1006_expand.h"
#include <time.h>
#include <stdio.h>
#include "jbig.h"
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
void hp_jbig_checkpoint(void) {
#ifdef ESP_PLATFORM
    vTaskDelay(1);
#endif
}

typedef union alloc_node {
    struct { union alloc_node *prev,*next; size_t size; } a;
    max_align_t align;
} alloc_node;
static _Thread_local alloc_node *allocs;
static _Thread_local jmp_buf *oom;
void hp_jbig_oom(void) {
#ifdef ESP_PLATFORM
    ESP_LOGE("hp_p1006","JBIG allocation failed: aborting this print job");
#endif
    if (oom) longjmp(*oom,1);
    abort();
}
void *hp_jbig_malloc(size_t n) {
    if (n>SIZE_MAX-sizeof(alloc_node)) return NULL;
#ifdef ESP_PLATFORM
    // JBIG's many small blocks must not consume scarce internal Wi-Fi/USB RAM.
    alloc_node *a=heap_caps_malloc(sizeof(*a)+n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    alloc_node *a=malloc(sizeof(*a)+n);
#endif
    if (!a) return NULL;
    a->a.prev=NULL; a->a.next=allocs; a->a.size=n;
    if (allocs) allocs->a.prev=a;
    allocs=a; return a+1;
}
void hp_jbig_free(void *p) {
    if (!p) return;
    alloc_node *a=(alloc_node *)p-1;
    if (a->a.prev) a->a.prev->a.next=a->a.next; else allocs=a->a.next;
    if (a->a.next) a->a.next->a.prev=a->a.prev;
    free(a);
}
void *hp_jbig_realloc(void *p,size_t n) {
    if (!p) return hp_jbig_malloc(n);
    alloc_node *a=(alloc_node *)p-1; void *q=hp_jbig_malloc(n);
    if (!q) return NULL;
    memcpy(q,p,n<a->a.size?n:a->a.size); hp_jbig_free(p); return q;
}

typedef struct {
    printer_io_t io;
    int paper, scale_shift, width, height, stride, wire_stride, src_w, src_h, bpp, y;
    unsigned ink_pixels;
    unsigned char *raw, *chunk, *expanded;
    size_t used;
    unsigned char bih[20];
    bool first, header, failed, open, p1006;
    uint64_t expand_us, encode_us;
} hp_job;
// P1006's native mode is 2 bits/pixel at 600 video pixels/inch. Keep
// the 600-dpi one-bit image in PSRAM and expand just one JBIG stripe, including
// its two predecessor rows, instead of allocating an 8+ MB two-bit page.
#define HP_STRIPE_ROWS 128
static _Thread_local hp_job *encoding_job;
static uint64_t hp_now_us(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000+(uint64_t)t.tv_nsec/1000;
}
unsigned char *hp_jbig_stripe(unsigned long y,unsigned long rows,unsigned long width,int layer,int plane)
{
    hp_job *j=encoding_job;
    if (!j) return NULL;
    if (layer || plane || rows!=HP_STRIPE_ROWS || width!=(unsigned)j->wire_stride*8) hp_jbig_oom();
    uint64_t started = j->p1006 ? hp_now_us() : 0;
    memset(j->expanded,0,(HP_STRIPE_ROWS+2)*j->wire_stride);
    for (int r=0;r<HP_STRIPE_ROWS+2;r++) {
        long sy=(long)y+r-2;
        if (sy<0 || sy>=j->height) continue;
        const unsigned char *in=j->raw+(size_t)sy*j->stride;
        unsigned char *out=j->expanded+(size_t)r*j->wire_stride;
        if (j->p1006)
            hp_p1006_expand_row(out,in,(unsigned)j->width);
        else
            for (int x=0;x<j->width;x++) if(in[x>>3] & (0x80>>(x&7)))
                out[x>>2] |= 0xc0>>((x&3)*2); // P1008 retains its original path
    }
    if (j->p1006) j->expand_us += hp_now_us()-started;
    return j->expanded+2*j->wire_stride;
}
static int put(hp_job *j, const void *p, size_t n) {
    if (j->failed) return -1;
    if (j->io.write(j->io.ctx, p, n) < 0) j->failed = true;
    return j->failed ? -1 : 0;
}
static void u32(hp_job *j, unsigned v) {
    unsigned char b[] = {v >> 24, v >> 16, v >> 8, v}; put(j,b,4);
}
static void rec(hp_job *j, unsigned type, unsigned n) { u32(j,type); u32(j,n); }
static void item(hp_job *j, unsigned type, unsigned v) { u32(j,type); u32(j,4); u32(j,v); }
static void block(hp_job *j) {
    if (!j->used || j->failed) return;
    rec(j,5,4); item(j,0x80000000,j->first ? 64 : 48); item(j,0x40000000,0);
    if (j->first) { u32(j,0x40000002); u32(j,20); put(j,j->bih,20); }
    else item(j,0x40000003,1);
    item(j,0x80000001,0xdeadbeef);
    rec(j,7,j->used); put(j,j->chunk,j->used); rec(j,6,0);
    j->first = false; j->used = 0;
    if (j->io.progress) j->io.progress(j->io.ctx,j->height,j->height);
}
static void encoded(unsigned char *p, size_t n, void *ctx) {
    hp_job *j = ctx;
    if (!j->header) {
        if (n != 20) { j->failed = true; return; }
        memcpy(j->bih,p,20); j->header = true; return;
    }
    while (n && !j->failed) {
        size_t a = n < 65536-j->used ? n : 65536-j->used;
        memcpy(j->chunk+j->used,p,a); j->used+=a; p+=a; n-=a;
        if (j->used==65536) block(j);
    }
}
static const printer_paper_t papers[] = {
    {"A4","iso_a4_210x297mm",21000,29700}, {"Letter","na_letter_8.5x11in",21590,27940},
    {"A5","iso_a5_148x210mm",14800,21000}, {"A6","iso_a6_105x148mm",10500,14800}
};
static void *begin(const printer_driver_t *d, const printer_io_t *io, int dpi, int paper) {
    bool valid_dpi = dpi == 600;
#if defined(PRINTSERVER_PROFILE_HP) && !defined(PRINTSERVER_PROFILE_HP_P1008)
    valid_dpi = valid_dpi || dpi == 300;
#endif
    if (!io || !io->write || !valid_dpi || paper<0 || paper>=d->npapers) return NULL;
    hp_job *j=calloc(1,sizeof(*j)); if (!j) return NULL;
    j->io=*io; j->paper=paper; j->p1006=!strcmp(d->id,"hp-p1006");
    j->scale_shift = dpi == 300;
    // foo2xqx's P1006 wrapper clips 96 dots (4.064 mm) at each edge.
    j->width=papers[paper].width*600/2540-192;
    j->height=papers[paper].height*600/2540-192;
    j->stride=((j->width+127)&~127)/8;
    j->wire_stride=((j->width*2+127)&~127)/8;
    j->raw=calloc(j->height,j->stride); j->chunk=malloc(65536);
    j->expanded=malloc((HP_STRIPE_ROWS+2)*j->wire_stride);
    if (!j->raw || !j->chunk || !j->expanded) { free(j->raw); free(j->chunk); free(j->expanded); free(j); return NULL; }
    static const char pre[]="\033%-12345X@PJL JOB\n@PJL SET JAMRECOVERY=OFF\n@PJL SET DENSITY=3\n@PJL SET ECONOMODE=OFF\n@PJL SET RET=MEDIUM\n@PJL INFO STATUS\n@PJL USTATUS DEVICE = ON\n@PJL USTATUS JOB = ON\n@PJL USTATUS PAGE = ON\n@PJL USTATUS TIMED = 30\n@PJL SET JOBATTR=\"JobAttr4=20261114000000\"";
    put(j,pre,sizeof(pre)); // includes required NUL before universal exit
    const char magic[]="\033%-12345X,XQX"; put(j,magic,sizeof(magic)-1);
    rec(j,1,7); item(j,0x80000000,84); item(j,0x10000005,1); item(j,0x10000001,0);
    item(j,0x10000002,0); item(j,0x10000000,0); item(j,0x10000003,1); item(j,0x80000001,0xdeadbeef);
    if (j->failed) { free(j->raw); free(j->chunk); free(j->expanded); free(j); return NULL; }
    return j;
}
static int page_begin(void *ctx,int w,int h,int bpp) {
    hp_job *j=ctx;
    if (j->failed || j->open || w<=0 || h<=0 || w>5200 || h>7100 || (bpp!=1 && bpp!=3)) return -1;
    j->src_w=w; j->src_h=h; j->bpp=bpp; j->y=0; j->open=true; j->ink_pixels=0;
    memset(j->raw,0,(size_t)j->stride*j->height); return 0;
}
#if defined(PRINTSERVER_PROFILE_HP) && !defined(PRINTSERVER_PROFILE_HP_P1008)
// At 300 dpi four input pixels fill one byte on each of two output rows.
// Read each source pixel once and keep the same four dither phases as mono_pack.
static inline unsigned pack_300_pair(uint8_t *top, uint8_t *bottom, const uint8_t *src,
                                     int width, int row, int bpp)
{
    static const uint8_t matrix[4][4] = {
        {8,136,40,168}, {200,72,232,104}, {56,184,24,152}, {248,120,216,88}
    };
    const uint8_t *a = matrix[row & 3], *b = matrix[(row + 1) & 3];
    unsigned ink = 0;
    for (int x = 0; x < width; x += 8) {
        unsigned upper = 0, lower = 0;
        if (bpp == 1) {
            // White text-page margins need no comparisons or bit packing.
            uint32_t pixels;
            memcpy(&pixels, src, sizeof(pixels));
            if (pixels == UINT32_MAX) {
                *top++ = 0;
                *bottom++ = 0;
                src += 4;
                continue;
            }
        }
        for (unsigned k = 0; k < 4; ++k, src += bpp) {
            unsigned gray = bpp == 1 ? src[0] : (77u * src[0] + 150u * src[1] + 29u * src[2]) >> 8;
            unsigned a0 = gray < a[(k & 1) * 2], a1 = gray < a[(k & 1) * 2 + 1];
            unsigned b0 = gray < b[(k & 1) * 2], b1 = gray < b[(k & 1) * 2 + 1];
            upper = (upper << 2) | (a0 << 1) | a1;
            lower = (lower << 2) | (b0 << 1) | b1;
            ink += a0 + a1 + b0 + b1;
        }
        *top++ = (uint8_t)upper;
        *bottom++ = (uint8_t)lower;
    }
    return ink;
}
#endif

static int line(void *ctx,int y,const uint8_t *p) {
    hp_job *j=ctx;
    if (!j->open || j->failed || y!=j->y || y>=j->src_h) return -1;
    j->y++;
    int row = (y << j->scale_shift) - 96;
    int rows = 1 << j->scale_shift;
#if defined(PRINTSERVER_PROFILE_HP) && !defined(PRINTSERVER_PROFILE_HP_P1008)
    if (p && j->p1006 && rows == 2 && row >= 0 && row + 1 < j->height &&
        j->width % 8 == 0 && j->src_w * 2 >= j->width + 96) {
        uint8_t *top = j->raw + (size_t)row * j->stride;
        if (j->bpp == 1)
            j->ink_pixels += pack_300_pair(top, top + j->stride, p + 48, j->width, row, 1);
        else
            j->ink_pixels += pack_300_pair(top, top + j->stride, p + 48 * 3, j->width, row, 3);
        return 0;
    }
#endif
    for (int offset = 0; p && offset < rows; ++offset) {
        int target = row + offset;
        if (target < 0 || target >= j->height)
            continue;
        unsigned char *out = j->raw + (size_t)target * j->stride;
        j->ink_pixels += mono_pack(out, j->width, p, j->src_w, j->bpp,
                                   96, j->scale_shift, target);
    }
    return 0;
}
static int page_end(void *ctx) {
    hp_job *j=ctx;
    if (!j->open || j->y!=j->src_h || j->failed) return -1;
#ifdef ESP_PLATFORM
    ESP_LOGI("hp_p1006","raster %dx%d source=%dx%d bpp=%d ink_pixels=%u; wire=1200x600/2bpp",
        j->width,j->height,j->src_w,j->src_h,j->bpp,j->ink_pixels);
#endif
    rec(j,3,15); item(j,0x80000000,180); item(j,0x20000005,1); item(j,0x20000006,7);
    item(j,0x20000000,1); item(j,0x20000007,1); item(j,0x20000008,600); item(j,0x20000009,600);
    item(j,0x2000000d,j->wire_stride*8); item(j,0x2000000e,j->height); item(j,0x2000000a,2);
    item(j,0x2000000f,j->width); item(j,0x20000010,j->height); item(j,0x20000011,0);
    // Windows DMPAPER values; small-sheet feed still needs hardware testing.
    static const unsigned paper_codes[]={9,1,11,70};
    item(j,0x20000001,paper_codes[j->paper]); item(j,0x80000001,0xdeadbeef);
    struct jbg_enc_state enc;
    unsigned char *planes[]={j->raw}; j->first=true; j->header=false; j->used=0;
    jmp_buf guard; oom=&guard; encoding_job=j;
    if (setjmp(guard)) {
        while (allocs) hp_jbig_free(allocs+1);
        oom=NULL; encoding_job=NULL; j->failed=true; return -1;
    }
    jbg_enc_init(&enc,j->wire_stride*8,j->height,1,planes,encoded,j);
    jbg_enc_options(&enc,JBG_ILEAVE|JBG_SMID,JBG_DELAY_AT|JBG_LRLTWO|JBG_TPDON|JBG_TPBON|JBG_DPON,128,0,0);
    uint64_t encode_start=j->p1006 ? hp_now_us() : 0;
    jbg_enc_out(&enc);
    if (j->p1006) j->encode_us += hp_now_us()-encode_start;
    jbg_enc_free(&enc); oom=NULL; encoding_job=NULL; block(j); rec(j,4,0); j->open=false;
#if defined(PRINTSERVER_PROFILE_HP) && !defined(PRINTSERVER_PROFILE_HP_P1008)
    // Complete the page transfer before returning to document decoding.
    if (!j->failed && j->io.flush && j->io.flush(j->io.ctx) < 0)
        j->failed = true;
#endif
    return j->failed ? -1:0;
}
#ifndef PRINTSERVER_PROFILE_HP_P1008
static void stats(void *ctx,char *buf,size_t n) {
    hp_job *j=ctx;
    snprintf(buf,n,"HP P1006 expand=%.3f s JBIG+output=%.3f s (expansion included)",
             j->expand_us/1e6,j->encode_us/1e6);
}
#endif
static int end(void *ctx) {
    hp_job *j=ctx;
    if (j->open) j->failed=true;
    if (!j->failed) { rec(j,2,0); const char tail[]="\033%-12345X@PJL EOJ\n\033%-12345X"; put(j,tail,sizeof(tail)-1); }
    int rc=j->failed ? -1:0;
    if (!rc && j->io.flush && j->io.flush(j->io.ctx)<0) rc=-1;
    free(j->raw); free(j->chunk); free(j->expanded); free(j); return rc;
}
#if defined(PRINTSERVER_PROFILE_HP) && !defined(PRINTSERVER_PROFILE_HP_P1008)
static int abort_job(void *ctx) {
    hp_job *j = ctx;
    // A page buffered for JBIG has no XQX header until page_end starts.
    // Drop an incomplete raster page and close the valid prefix of the job.
    // end() still reports failure after a partial USB write or encoder error.
    j->open = false;
    return end(j);
}
#endif
static int test(const printer_driver_t *d,const printer_io_t *io,int dpi,const char *label) {
    hp_job *j=begin(d,io,dpi,0); if (!j) return -1;
    int w=papers[0].width*dpi/2540,h=papers[0].height*dpi/2540;
    unsigned char *p=malloc(w); if (!p) { j->failed=true; return end(j); }
    page_begin(j,w,h,1);
    for (int y=0;y<h;y++) {
        memset(p,255,w);
        for (int x=120;x<w-120;x++) if (y>=120 && y<h-120 &&
            (y<126 || y>=h-126 || x<126 || x>=w-126 || (y>=400 && y<700)))
            p[x]=(y>=400 && y<700) ? (x*256/w):0;
        line(j,y,p); if (io->progress && !(y%64)) io->progress(io->ctx,y,h);
    }
    free(p); int rc=page_end(j); if (end(j)<0) rc=-1; return rc;
}
#ifdef PRINTSERVER_PROFILE_HP_P1008
// OpenPrinting ships the same XQX options and P1006 bootstrap for P1008.
// Require the actual P1008 VID/PID for the dedicated experimental profile.
const printer_driver_t hp_p1008_driver={
    .id="hp-p1008",.make_model="HP LaserJet P1008",
    .usb_vid=0x03f0,.usb_pid=0x4917,
#else
const printer_driver_t hp_p1006_driver={
    .id="hp-p1006",.make_model="HP LaserJet P1006",.usb_mfg="HP",.usb_mdl="LaserJet P1006",
    .usb_vid=0x03f0,.usb_pid=0x3e17,
#endif
    .color=false,.scanner=false,
#if defined(PRINTSERVER_PROFILE_HP) && !defined(PRINTSERVER_PROFILE_HP_P1008)
    .resolutions={300},.nresolutions=1,.papers=papers,
#else
    .resolutions={600},.nresolutions=1,.papers=papers,
#endif
#ifdef PRINTSERVER_PROFILE_HP_P1008
    .npapers=3,  // A4, Letter and A5 are present in the upstream P1008 PPD.
#else
    .npapers=4,
#endif
    .margin_left=407,.margin_right=407,.margin_top=407,.margin_bottom=407,
#ifndef PRINTSERVER_PROFILE_HP_P1008
    .job_stats=stats,
#endif
#if defined(PRINTSERVER_PROFILE_HP) && !defined(PRINTSERVER_PROFILE_HP_P1008)
    .job_abort=abort_job,
#endif
    .job_begin=begin,.page_begin=page_begin,.page_line=line,.page_end=page_end,.job_end=end,.test_page=test
};
