// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
typedef struct {
    uint8_t reply[1024]; size_t len, pos;
    unsigned pages, printed, video, offline, bytes, fail_read, bad_cmd;
    unsigned begin_pages, black_end, start_print, last_page_width;
    unsigned input_scale;
    unsigned row, height, left, top, crop_width, force_full, paper_code, paper_width, paper_height;
} fake_t;
static unsigned le(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static void put(uint8_t *p, unsigned v) { p[0]=v; p[1]=v>>8; }
static int write_device(void *ctx, const uint8_t *data, size_t n)
{
    fake_t *f=ctx;
    assert(n>=4 && le(data+2)==n);
    unsigned cmd=le(data); const uint8_t *p=data+4;
    f->bytes+=n;
    assert(f->pos==f->len); // the previous reply must have been fully consumed
    f->pos=f->len=0;
    if(cmd==CAPT_MULTI_COMMAND) {
        assert(n==68); // 4 wrapper + 44 page + 12 Hi-SCoA + 4 begin + 4 end
        assert(le(p)==CAPT_IC_BEGIN_PAGE && le(p+2)==44);
        f->last_page_width=le(p+4+26); // line size in bytes
        assert(f->last_page_width>0 && f->last_page_width<=610);
        f->paper_code=p[4+4];f->paper_width=le(p+4+30);f->paper_height=le(p+4+32);
        f->left=le(p+4+22); f->top=le(p+4+24); f->height=le(p+4+28);
        if (!f->crop_width) f->crop_width = f->paper_width==3496 ? (14800-950)*600/2540 :
            f->paper_width==2480 ? (10500-950)*600/2540 : f->last_page_width==592 ? 4736 : 4875;
        assert(f->last_page_width==(f->crop_width+7)/8);
        f->row=0;
        f->pages++; f->begin_pages++; return 0;
    }
    if(cmd==CAPT_IC_VIDEO_DATA) {
#ifndef CAPT_SIM_NO_RASTER_CHECK
        uint8_t output[610*70];
        const void *compressed=p; size_t compressed_n=n-4, output_n=sizeof(output);
        unsigned code=hiscoa_decompress_band(&compressed,&compressed_n,output,&output_n,
            f->last_page_width,&hiscoa_default_params);
        unsigned rows=f->height-f->row; if(rows>70) rows=70;
        assert(output_n==rows*f->last_page_width);
        // Upstream adds a full 32-bit padding word when EOB is already aligned.
        assert(compressed_n==0 || compressed_n==4);
        for(size_t k=0;k<compressed_n;k++) assert(((const uint8_t *)compressed)[k]==0xbc);
        assert(code==(f->row+rows==f->height?1:0));
        for(unsigned y=0;y<rows;y++) for(unsigned x=0;x<f->last_page_width*8;x++) {
            unsigned source_y=f->top+f->row+y;
            unsigned scale=f->input_scale ? f->input_scale : 1;
            unsigned expected=x<f->crop_width && (source_y/scale)%5 && !(((x+f->left)/scale+source_y/scale)&1);
            assert(!!(output[y*f->last_page_width+x/8] & (0x80>>(x&7)))==expected);
        }
        f->row+=rows;
#endif
        f->video++; return 0;
    }
    if(cmd==CAPT_IC_BLACK_END) { 
#ifndef CAPT_SIM_NO_RASTER_CHECK
        assert(f->row==f->height);
#endif
 f->black_end++; return 0; }
    if(cmd==CAPT_GO_OFFLINE) f->offline=1;
    if(cmd==CAPT_GO_ONLINE) f->offline=0;
    if(cmd==CAPT_START_PRINT) { assert(n==6 && le(p)==f->pages); f->printed=le(p); f->start_print++; }
    size_t payload=2;
    // Real LBP2900 customer log: 56 wire bytes, including the 4-byte header.
    if(cmd==CAPT_GET_PRINTER_INFO) payload=52;
    if(cmd==CAPT_RESERVE_UNIT) payload=4;
    if(cmd==CAPT_GET_BASIC_STATUS) payload=10;
    if(cmd==CAPT_GET_EXTENDED_STATUS) payload=40;
    memset(f->reply,0,sizeof(f->reply));
    put(f->reply,cmd+(f->bad_cmd?1:0)); put(f->reply+2,payload+4);
    uint8_t *s=f->reply+4;
    if(cmd==CAPT_GET_PRINTER_INFO) { put(s+6,65280); put(s+8,64); }
    if(cmd==CAPT_RESERVE_UNIT) put(s+2,1);
    if(cmd==CAPT_GET_BASIC_STATUS || cmd==CAPT_GET_EXTENDED_STATUS) {
        s[0]=f->offline?0x10:0; s[1]=1; s[4]=(f->force_full && f->video>1 && f->printed<f->pages)?0:15;
        if(payload>=40) { put(s+14,f->pages); put(s+16,f->printed); put(s+18,f->printed); put(s+20,f->printed); }
    }
    f->len=payload+4;
    return 0;
}
static int flush_device(void *ctx) { (void)ctx; return 0; }
static int read_device(void *ctx, uint8_t *buf, size_t n, unsigned timeout)
{
    fake_t *f=ctx; (void)timeout;
    if(f->fail_read) return -1;
    assert(f->len>f->pos);
    if(n>3) n=3; // deliberately fragmented headers and payloads
    if(n>f->len-f->pos) n=f->len-f->pos;
    memcpy(buf,f->reply+f->pos,n); f->pos+=n; return n;
}
static printer_io_t io(fake_t *f) { return (printer_io_t){.ctx=f,.write=write_device,.read=read_device,.flush=flush_device}; }
