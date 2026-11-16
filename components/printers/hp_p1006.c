// SPDX-License-Identifier: GPL-3.0-or-later
// XQX framing adapted from Rick Richardson's foo2xqx (GPL-2.0-or-later).
// Experimental HP LaserJet P1006, 600 dpi monochrome, simplex A4 / Letter.
#include "printer_driver.h"
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
    int paper, width, height, stride, wire_stride, src_w, src_h, bpp, y;
    unsigned ink_pixels;
    unsigned char *raw, *chunk, *expanded;
    size_t used;
    unsigned char bih[20];
    bool first, header, failed, open;
} hp_job;
// P1006's native mode is 2 bits/pixel at 600 video pixels/inch. Keep
// the 600-dpi one-bit image in PSRAM and expand just one JBIG stripe, including
// its two predecessor rows, instead of allocating an 8+ MB two-bit page.
#define HP_STRIPE_ROWS 128
static _Thread_local hp_job *encoding_job;
unsigned char *hp_jbig_stripe(unsigned long y,unsigned long rows,unsigned long width,int layer,int plane)
{
    hp_job *j=encoding_job;
    if (!j) return NULL;
    if (layer || plane || rows!=HP_STRIPE_ROWS || width!=(unsigned)j->wire_stride*8) hp_jbig_oom();
    memset(j->expanded,0,(HP_STRIPE_ROWS+2)*j->wire_stride);
    for (int r=0;r<HP_STRIPE_ROWS+2;r++) {
        long sy=(long)y+r-2;
        if (sy<0 || sy>=j->height) continue;
        const unsigned char *in=j->raw+(size_t)sy*j->stride;
        unsigned char *out=j->expanded+(size_t)r*j->wire_stride;
        for (int x=0;x<j->width;x++) if(in[x>>3] & (0x80>>(x&7)))
            out[x>>2] |= 0xc0>>((x&3)*2); // white=00, black=11
    }
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
    {"A4","iso_a4_210x297mm",21000,29700}, {"Letter","na_letter_8.5x11in",21590,27940}
};
static void *begin(const printer_driver_t *d, const printer_io_t *io, int dpi, int paper) {
    if (!io || !io->write || dpi!=600 || paper<0 || paper>=2) return NULL;
    hp_job *j=calloc(1,sizeof(*j)); if (!j) return NULL;
    j->io=*io; j->paper=paper;
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
static int line(void *ctx,int y,const uint8_t *p) {
    static const unsigned char matrix[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    hp_job *j=ctx;
    if (!j->open || j->failed || y!=j->y || y>=j->src_h) return -1;
    j->y++;
    int row=y-96;
    if (p && row>=0 && row<j->height) {
        unsigned char *out=j->raw+(size_t)row*j->stride;
        for (int x=0;x<j->width && x+96<j->src_w;x++) {
            int off=(x+96)*j->bpp;
            unsigned gray=j->bpp==1 ? p[off] : (77*p[off]+150*p[off+1]+29*p[off+2])>>8;
            if (gray < (unsigned)matrix[row&3][x&3]*16+8) { out[x>>3]|=0x80>>(x&7); j->ink_pixels++; }
        }
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
    item(j,0x20000001,j->paper ? 1:9); item(j,0x80000001,0xdeadbeef);
    struct jbg_enc_state enc;
    unsigned char *planes[]={j->raw}; j->first=true; j->header=false; j->used=0;
    jmp_buf guard; oom=&guard; encoding_job=j;
    if (setjmp(guard)) {
        while (allocs) hp_jbig_free(allocs+1);
        oom=NULL; encoding_job=NULL; j->failed=true; return -1;
    }
    jbg_enc_init(&enc,j->wire_stride*8,j->height,1,planes,encoded,j);
    jbg_enc_options(&enc,JBG_ILEAVE|JBG_SMID,JBG_DELAY_AT|JBG_LRLTWO|JBG_TPDON|JBG_TPBON|JBG_DPON,128,0,0);
    jbg_enc_out(&enc); jbg_enc_free(&enc); oom=NULL; encoding_job=NULL; block(j); rec(j,4,0); j->open=false;
    return j->failed ? -1:0;
}
static int end(void *ctx) {
    hp_job *j=ctx;
    if (j->open) j->failed=true;
    if (!j->failed) { rec(j,2,0); const char tail[]="\033%-12345X@PJL EOJ\n\033%-12345X"; put(j,tail,sizeof(tail)-1); }
    int rc=j->failed ? -1:0;
    if (!rc && j->io.flush && j->io.flush(j->io.ctx)<0) rc=-1;
    free(j->raw); free(j->chunk); free(j->expanded); free(j); return rc;
}
static int test(const printer_driver_t *d,const printer_io_t *io,int dpi,const char *label) {
    hp_job *j=begin(d,io,dpi,0); if (!j) return -1;
    int w=papers[0].width*600/2540,h=papers[0].height*600/2540;
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
const printer_driver_t hp_p1006_driver={
    .id="hp-p1006",.make_model="HP LaserJet P1006",.usb_mfg="HP",.usb_mdl="LaserJet P1006",
    .usb_vid=0x03f0,.usb_pid=0x3e17,.color=false,.scanner=false,
    .resolutions={600},.nresolutions=1,.papers=papers,.npapers=2,
    .margin_left=407,.margin_right=407,.margin_top=407,.margin_bottom=407,
    .job_begin=begin,.page_begin=page_begin,.page_line=line,.page_end=page_end,.job_end=end,.test_page=test
};
