#include "printer_driver.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int sink(void *ctx,const uint8_t *p,size_t n) { return fwrite(p,1,n,ctx)==n?0:-1; }
static int fail(void *ctx,const uint8_t *p,size_t n) { return -1; }
int main(int argc,char **argv) {
    const char *ids[]={"generic-pcl5","generic-postscript"};
    const char *cmds[]={"PCL","PCL5","PCL5e","PCL5c","POSTSCRIPT","PostScript2","POSTSCRIPT3","PS"};
    for(int k=0;k<8;k++) {
        char id[128]; snprintf(id,sizeof(id),"MFG:Test;MDL:Test;CMD:PJL, %s ,PCLXL;",cmds[k]);
        const printer_driver_t *d=printer_driver_match(0,0,id); assert(d && !strcmp(d->id,ids[k>=4]));
    }
    const char *reject[]={"PCLXL","PCL6","PCL3GUI","PCLm","SPL","UFRII","PCL5xyz","PSX","ZJS"};
    for(int k=0;k<9;k++) { char id[80];snprintf(id,sizeof(id),"CMD:%s;",reject[k]);assert(!printer_driver_match(0,0,id)); }
    assert(!strcmp(printer_driver_match(0x04a9,0x2676,"CMD:PCL;")->id,"canon-lbp2900"));
    assert(!strcmp(printer_driver_match(0x03f0,0x3e17,"CMD:POSTSCRIPT;")->id,"hp-p1006"));
    for(int lang=0;lang<2;lang++) for(int paper=0;paper<2;paper++) for(int dpi=300;dpi<=600;dpi+=300) {
        const printer_driver_t *d=printer_driver_find(ids[lang]); assert(d);
        assert(!d->job_begin(d,&(printer_io_t){.write=fail},dpi,paper));
        char path[256]; snprintf(path,sizeof(path),"%s-%d-%d-%d.%s",argc>1?argv[1]:"/tmp/standard",lang,paper,dpi,lang?"ps":"pcl");
        FILE *f=fopen(path,"wb");assert(f); printer_io_t io={.write=sink,.ctx=f};
        void *j=d->job_begin(d,&io,dpi,paper);assert(j);
        int w=d->papers[paper].width*dpi/2540,h=d->papers[paper].height*dpi/2540;
        unsigned char *p=malloc(w*3);assert(p);
        for(int page=0;page<2;page++) {
            int bpp=page?3:1; assert(!d->page_begin(j,w,h,bpp));assert(d->page_line(j,1,NULL)<0);
            for(int y=0;y<h;y++) {
                for(int x=0;x<w;x++) for(int k=0;k<bpp;k++) p[x*bpp+k]=(x>w/3 && x<2*w/3 && y>h/3 && y<2*h/3)?0:255;
                assert(!d->page_line(j,y,p));
            }
            assert(!d->page_end(j));
        }
        assert(!d->job_end(j)); free(p); fclose(f);
    }
    puts("Standard drivers: exact capability tokens, unsafe fallbacks refused, A4/Letter, 300/600 dpi, gray/RGB two-page streams passed");
}
