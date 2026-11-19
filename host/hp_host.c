#include "printer_driver.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int write_file(void *ctx,const uint8_t *p,size_t n) { return fwrite(p,1,n,ctx)==n?0:-1; }
static int fail(void *ctx,const uint8_t *p,size_t n) { return -1; }
int main(int argc,char **argv) {
    const printer_driver_t *d=printer_driver_match(0x03f0,0x3e17,NULL);
    assert(d && !strcmp(d->id,"hp-p1006") && !d->scanner && !d->color);
    assert(printer_driver_match(0,0,"MFG:HP;MDL:LaserJet P1006;")==d);
    assert(!d->job_begin(d,&(printer_io_t){.write=fail},600,0));
    assert(!d->job_begin(d,&(printer_io_t){.write=fail},300,0));
    for (int paper=0;paper<d->npapers;paper++) {
        char path[200]; snprintf(path,sizeof(path),"%s-%d.xqx",argc>1?argv[1]:"/tmp/hp",paper);
        FILE *out=fopen(path,"wb"); assert(out);
        printer_io_t io={.write=write_file,.ctx=out}; void *j=d->job_begin(d,&io,600,paper); assert(j);
        int w=d->papers[paper].width*600/2540,h=d->papers[paper].height*600/2540;
        unsigned char *line=malloc(w*3); assert(line);
        for (int page=0;page<2;page++) {
            int bpp=page?3:1; assert(d->page_begin(j,w,h,bpp)==0);
            assert(d->page_line(j,1,NULL)<0);
            for (int y=0;y<h;y++) {
                for(int x=0;x<w;x++) { unsigned char v=((x/37+y/29+page)%3)?255:0;
                    for(int k=0;k<bpp;k++) line[x*bpp+k]=v; }
                assert(d->page_line(j,y,line)==0);
            }
            assert(d->page_end(j)==0);
        }
        free(line); assert(d->job_end(j)==0); fclose(out);
    }
    puts("HP P1006: USB/model matching, A4/Letter/A5/A6, gray/RGB 2-page streams, bad sequence and sink failure passed");
}
