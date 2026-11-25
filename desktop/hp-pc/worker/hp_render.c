// SPDX-License-Identifier: GPL-3.0-or-later
// Desktop adapter: the production raster decoder and HP encoder stay unchanged.
#include "printer_driver.h"
#include "raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const printer_driver_t hp_p1006_driver;
typedef struct { FILE *out; void *job; int dpi,paper,pages; } context;
typedef struct { FILE *file; bool urf; } input;
static int read_file(void *ctx,uint8_t *p,size_t n) {
    input *in=ctx;FILE *f=in->file;bool first=ftell(f)==0;
    size_t k=fread(p,1,n,f);
    // Decode URF to actual EOF, then compare its original declared page count.
    // The streaming decoder otherwise stops at that count and ignores trailing pages.
    if(in->urf && first && k>=12)memset(p+8,0,4);
    return k?(int)k:ferror(f)?-1:0;
}
static int write_file(void *ctx,const uint8_t *p,size_t n) { return fwrite(p,1,n,ctx)==n?0:-1; }
static int page_begin(void *ctx,const raster_page_t *pg) {
    context *c=ctx;const printer_driver_t *d=&hp_p1006_driver;
    if(pg->dpi!=300 && pg->dpi!=600)return RASTER_ERR_UNSUPPORTED;
    int paper=-1;
    for(int i=0;i<d->npapers;i++) {
        int w=d->papers[i].width*pg->dpi/2540,h=d->papers[i].height*pg->dpi/2540;
        if(abs(w-pg->width)<=2 && abs(h-pg->height)<=2){paper=i;break;}
    }
    if(paper<0 || (c->job && (c->dpi!=pg->dpi || c->paper!=paper)))return RASTER_ERR_UNSUPPORTED;
    if(!c->job){printer_io_t io={.write=write_file,.ctx=c->out};c->dpi=pg->dpi;c->paper=paper;c->job=d->job_begin(d,&io,pg->dpi,paper);}
    return c->job?d->page_begin(c->job,pg->width,pg->height,pg->bytes_pp):RASTER_ERR_NOMEM;
}
static int line(void *ctx,int y,const uint8_t *p) { return hp_p1006_driver.page_line(((context*)ctx)->job,y,p); }
static int repeat(void *ctx,int y,const uint8_t *p,bool r) {
    void *j=((context*)ctx)->job;
    return hp_p1006_driver.page_line_repeat?hp_p1006_driver.page_line_repeat(j,y,p,r):hp_p1006_driver.page_line(j,y,p);
}
static int page_end(void *ctx,const raster_page_t *pg) { (void)pg;context*c=ctx;int ret=hp_p1006_driver.page_end(c->job);if(!ret)c->pages++;return ret; }
int main(int argc,char **argv) {
    if(argc!=3){fprintf(stderr,"usage: hp-render input.raster output.xqx\n");return 2;}
    size_t n=strlen(argv[2])+5;char *tmp=malloc(n);if(!tmp)return 2;snprintf(tmp,n,"%s.tmp",argv[2]);
    FILE *in=fopen(argv[1],"rb"),*out=fopen(tmp,"wb");
    if(!in || !out){if(in)fclose(in);if(out)fclose(out);remove(tmp);free(tmp);return 2;}
    unsigned declared=0;unsigned char signature[12];
    bool urf=fread(signature,1,12,in)==12 && !memcmp(signature,"UNIRAST\0",8);
    if(urf)
        declared=(unsigned)signature[8]<<24|(unsigned)signature[9]<<16|(unsigned)signature[10]<<8|signature[11];
    rewind(in);
    context c={.out=out};raster_cb_t cb={.page_begin=page_begin,.line=line,.line_repeat=repeat,.page_end=page_end};
    input source={.file=in,.urf=urf};
    int pages=raster_decode(read_file,&source,&cb,&c);
    bool ok=pages>0 && pages==c.pages && (!declared || declared==(unsigned)pages) && !ferror(in);
    fclose(in);
    if(c.job){int ret=ok?hp_p1006_driver.job_end(c.job):hp_p1006_driver.job_abort(c.job);if(ret)ok=false;}
    if(ferror(out))ok=false;if(fclose(out))ok=false;
    if(ok && rename(tmp,argv[2]))ok=false;
    if(!ok){remove(tmp);fprintf(stderr,"Raster conversion refused: %s (pages=%d). Nothing sent to ESP.\n",raster_strerror(pages),c.pages);}
    else printf("{\"pages\":%d,\"dpi\":%d}\n",pages,c.dpi);
    free(tmp);return ok?0:1;
}
