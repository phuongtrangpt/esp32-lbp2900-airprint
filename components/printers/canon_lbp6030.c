// SPDX-License-Identifier: MIT
// Experimental USB adaptation of juliusneu/lbp6030-printer-app (2ce3542892db).
// Protocol source tested by upstream ONLY on LBP6030w over TCP; USB is unverified.
// Copyright (c) 2026 juliusneu. See components/lbp6030/LICENSE.
#include "printer_driver.h"
#include "lbp6030.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "lbp6030_prefix.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define buffer_alloc(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define buffer_alloc(n) malloc(n)
#endif
#define BAND_ROWS 8
#define STREAM_CAP 31000
static const printer_paper_t papers[]={
 {"A4","iso_a4_210x297mm",21000,29700},{"Letter","na_letter_8.5x11in",21590,27940}};
typedef struct {
 printer_io_t io; uint32_t total,pages;
 int paper,w,h,stride,sw,sh,bpp,y,oy,rows,open,failed;
 uint8_t *raw,*encoded,*previous,*full,*packet_buf;
} job_t;
static void be16(uint8_t *p,unsigned v){p[0]=v>>8;p[1]=v;}
static void be32(uint8_t *p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static int put(job_t *j,const uint8_t *p,size_t n){
 if(j->failed)return -1;
 if(n>UINT32_MAX-j->total || j->io.write(j->io.ctx,p,n)<0){j->failed=1;return -1;}
 j->total+=(uint32_t)n;return 0;
}
static int packet(job_t *j,unsigned op,const uint8_t *p,size_t n,unsigned flag){
 uint8_t h[20]={0xcd,0xca,0x10,0,0,0,0,1,0,0,0xff,0xff};
 if(n>4076){j->failed=1;return -1;}
 h[3]=flag;be16(h+4,op);be16(h+8,n);
 if(put(j,h,20)<0)return -1;
 return n?put(j,p,n):0;
}
static int data(job_t *j,const uint8_t *p,size_t n){
 // Each caller flushes at band boundaries (<32KiB), well below 64KiB rule.
 uint8_t *chunk=j->packet_buf;chunk[0]=1;
 while(n){size_t k=n>4075?4075:n;memcpy(chunk+1,p,k);
  if(packet(j,0x1a,chunk,k+1,2)<0)return -1;
  p+=k;n-=k;}
 return 0;
}
static int flush_data(job_t *j){const uint8_t one=1;return packet(j,0x1a,&one,1,2);}
static int emit_band(job_t *j,int last){
 if(!j->rows)return 0;
 size_t n=lbp6030_encode(j->raw,j->stride,j->rows,last,j->encoded,STREAM_CAP);
 if(!n || n+14>=32768){j->failed=1;return -1;}
 uint8_t h[48];size_t k=0;h[k++]=0x62;h[k++]=0xe3;h[k++]=0x85;
 be16(h+k,j->w);k+=2;be16(h+k,j->rows);k+=2;
 int y=j->oy-j->rows;h[k++]=0xe8;
 if(y<128){h[k++]=0xa1;h[k++]=0;h[k++]=y;}
 else {h[k++]=0xa5;be16(h+k,0);k+=2;be16(h+k,y);k+=2;}
 h[k++]=0xe1;h[k++]=3;h[k++]=0xd7;
 unsigned blk=n+14;
 if(blk<128){h[k++]=0xa0;h[k++]=blk;}else{h[k++]=0xa4;be16(h+k,blk);k+=2;}
 if(blk<256){h[k++]=0x9c;h[k++]=blk;}else{h[k++]=0x9d;be16(h+k,blk);k+=2;}
 const uint8_t params[]={8,4,3,1,2,0,80,0};memcpy(h+k,params,8);k+=8;
 h[k++]=last?0:1;h[k++]=n;h[k++]=n>>8;h[k++]=n>>16;h[k++]=n>>24;
 uint8_t tail=0x80;
 if(data(j,h,k)<0 || data(j,j->encoded,n)<0 || data(j,&tail,1)<0 || flush_data(j)<0)return -1;
 j->rows=0;return 0;
}
static void *begin(const printer_driver_t *d,const printer_io_t *io,int dpi,int paper){
 (void)d;if(!io || !io->write || dpi!=600 || paper<0 || paper>1)return NULL;
 job_t *j=calloc(1,sizeof(*j));if(!j)return NULL;
 j->io=*io;j->paper=paper;j->w=paper?5100:4958;j->h=paper?6600:7016;j->stride=((j->w*4+31)/32)*4;
 j->raw=buffer_alloc(BAND_ROWS*j->stride);j->encoded=buffer_alloc(STREAM_CAP);
 j->packet_buf=buffer_alloc(4076);
 j->previous=buffer_alloc(j->w);j->full=buffer_alloc(j->w);
 if(!j->raw || !j->encoded || !j->previous || !j->full || !j->packet_buf)goto fail;
 if(put(j,paper?prefix_letter:prefix_a4,paper?sizeof(prefix_letter):sizeof(prefix_a4))<0)goto fail;
 return j;
fail:free(j->raw);free(j->encoded);free(j->previous);free(j->full);free(j->packet_buf);free(j);return NULL;
}
static int page_begin(void *ctx,int w,int h,int bpp){
 job_t *j=ctx;if(j->failed || j->open || w<1 || h<1 || w>5200 || h>7100 || (bpp!=1 && bpp!=3))return -1;
 j->sw=w;j->sh=h;j->bpp=bpp;j->y=j->oy=j->rows=0;j->open=1;
 memset(j->previous,255,j->w);memset(j->full,255,j->w);
 if(!j->pages){
 uint8_t s[]={1,0xc1,0x85,0x10,0,0x10,0x89,0xc2,0,0xd8,0x84,2,0x58,0xdd,0x80,0xc8,
  0xf0,0x84,8,0,0xe9,0x84,1,0x90,2,0xc3,0,0xc5,0,0xc6,0,0x51,0xf2,0};
 s[26]=j->paper?100:0;if(data(j,s,sizeof(s))<0)return -1;
 }
 uint8_t p[]={3,0xe7,0x85,0,0,0,0,0xde,0x80,0,0xc8,0,0xca,0xa1,0,0,0xcb,0};
 be16(p+3,j->w);be16(p+5,j->h);
 const uint8_t body[]={0x61,0xe6,0x80,4,0xe5,0};
 if(data(j,p,sizeof(p))<0 || flush_data(j)<0)return -1;
 return data(j,body,sizeof(body));
}
static unsigned gray(job_t *j,const uint8_t *p,int x){
 if(!p || x>=j->sw)return 255;
 if(j->bpp==1)return p[x];
 p+=x*3;return (77*p[0]+150*p[1]+29*p[2])>>8;
}
static int raster_row(job_t *j,const uint8_t *p,int save_previous,int odd){
 uint8_t *dst=j->raw+j->rows*j->stride;memset(dst,0,j->stride);
 for(int x=0;x<j->w;x++){
  unsigned g=gray(j,p,x),full=odd?j->full[x]:g,half=odd?g:j->previous[x];
  // Area average from 600 dpi to 400 dpi; 0=white, 14=black.
  unsigned level=((3*255-2*full-half)*14+382)/(3*255);
  dst[x/2]|=level<<((x&1)?0:4);
  if(save_previous)j->previous[x]=g;
 }
 j->oy++;j->rows++;
 int last=j->oy==(j->h*2+2)/3;
 return (j->rows==BAND_ROWS || last)?emit_band(j,last):0;
}
static int consume(job_t *j,int y,const uint8_t *p){
 if(y>=j->h)return 0;
 if(y%3==0)return raster_row(j,p,0,0);
 if(y%3==1){for(int x=0;x<j->w;x++)j->full[x]=gray(j,p,x);return 0;}
 return raster_row(j,p,1,1);
}
static int line(void *ctx,int y,const uint8_t *p){
 job_t *j=ctx;if(!j->open || j->failed || y!=j->y || y>=j->sh){j->failed=1;return -1;}
 j->y++;return consume(j,y,p);
}
static int page_end(void *ctx){
 job_t *j=ctx;if(!j->open || j->failed || j->y!=j->sh){j->failed=1;return -1;}
 for(int y=j->y;y<j->h;y++)if(consume(j,y,NULL)<0)return -1;
 if(j->h%3==2 && raster_row(j,NULL,0,1)<0)return -1;
 const uint8_t end=0x13;
 if(data(j,&end,1)<0 || flush_data(j)<0)return -1;
 j->open=0;j->pages++;return 0;
}
static int end(void *ctx){
 job_t *j=ctx;if(j->open)j->failed=1;
 if(!j->failed){const uint8_t e[]={0x12,0x11},one=1;uint8_t count[6]={1,0x13};be32(count+2,j->pages);
 data(j,e,2);packet(j,0x1a,&one,1,0);packet(j,0x19,NULL,0,0);packet(j,0x15,count,6,0);
 packet(j,0x16,NULL,0,0);packet(j,0x12,count,6,0);
 uint8_t size[6]={7,0xd6};if(j->total>UINT32_MAX-47)j->failed=1;else be32(size+2,j->total+47);
 packet(j,0x12,size,6,0);const uint8_t zero=0;packet(j,0x13,&zero,1,0);
 }
 int r=j->failed?-1:0;if(!r && j->io.flush && j->io.flush(j->io.ctx)<0)r=-1;
 free(j->raw);free(j->encoded);free(j->previous);free(j->full);free(j->packet_buf);free(j);return r;
}
static void stats(void *ctx,char *s,size_t n){job_t *j=ctx;snprintf(s,n,"LBP6030 experimental USB: %u pages; %u bytes; bounded 8-row raster",(unsigned)j->pages,(unsigned)j->total);}
static int test(const printer_driver_t *d,const printer_io_t *io,int dpi,const char *label){
 (void)label;job_t *j=begin(d,io,dpi,0);if(!j)return -1;
 uint8_t *row=malloc(j->w);if(!row){j->failed=1;return end(j);}
 int r=page_begin(j,j->w,j->h,1);
 for(int y=0;y<j->h && !r;y++){memset(row,255,j->w);
 for(int x=150;x<j->w-150;x++)if(y>=150 && y<j->h-150 && (y<160 || y>=j->h-160 || x<160 || x>=j->w-160 || (y>=600 && y<900 && (x/300)%2)))row[x]=0;
 r=line(j,y,row);if(io->progress && !(y%64))io->progress(io->ctx,y,j->h);}
 free(row);if(!r)r=page_end(j);if(end(j)<0)r=-1;return r;
}
const printer_driver_t canon_lbp6030_driver={.id="canon-lbp6030",.make_model="Canon LBP6030 experimental",
 .usb_mfg="Canon",.usb_mdl="LBP6030/6040/6018L",.color=false,.resolutions={600},.nresolutions=1,
 .papers=papers,.npapers=2,.margin_left=500,.margin_right=500,.margin_top=500,.margin_bottom=500,
 .job_begin=begin,.page_begin=page_begin,.page_line=line,.page_end=page_end,.job_end=end,.job_stats=stats,.test_page=test};
