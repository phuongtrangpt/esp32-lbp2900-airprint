#include "printer_driver.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern const printer_driver_t canon_lbp6030_driver;
typedef struct {FILE *f;size_t calls,fail;} sink_t;
static int write_bytes(void *ctx,const uint8_t *p,size_t n){sink_t *s=ctx;if(++s->calls==s->fail)return -1;return s->f && fwrite(p,1,n,s->f)!=n?-1:0;}
int main(int argc,char **argv){
 const printer_driver_t *d=&canon_lbp6030_driver;
 sink_t s={0};if(argc>1){s.f=fopen(argv[1],"wb");assert(s.f);}
 printer_io_t io={.write=write_bytes,.ctx=&s};
 assert(!d->job_begin(d,&io,300,0));assert(!d->job_begin(d,&io,600,2));
 int paper=argc>2?atoi(argv[2]):0,pages=argc>3?atoi(argv[3]):2;
 void *j=d->job_begin(d,&io,600,paper);assert(j);
 int w=paper?5100:4960,h=paper?6600:7016;
 uint8_t *row=malloc(w);assert(row);
 for(int page=0;page<pages;page++){
  assert(!d->page_begin(j,w,h,1));
  for(int y=0;y<h;y++){
   for(int x=0;x<w;x++)row[x]=(x*13+y*7+page*89)%256;
   assert(!d->page_line(j,y,row));
  }
  assert(!d->page_end(j));
 }
 assert(!d->job_end(j));if(s.f)fclose(s.f);s.f=NULL;
 // disconnect and cancel must not append a successful footer
 for(size_t fail=1;fail<22;fail++){
  s.calls=0;s.fail=fail;j=d->job_begin(d,&io,600,0);
  if(j){int r=d->page_begin(j,w,h,1);for(int y=0;y<h && !r;y++)r=d->page_line(j,y,row);if(!r)r=d->page_end(j);assert(d->job_end(j)<0);}
 }
 s.fail=0;j=d->job_begin(d,&io,600,0);assert(j);assert(!d->page_begin(j,w,h,1));assert(d->page_line(j,2,row)<0);assert(d->job_end(j)<0);
 free(row);fprintf(stderr,"LBP6030: %d pages, disconnects, order rejection OK\n",pages);return 0;
}
