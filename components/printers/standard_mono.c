// SPDX-License-Identifier: GPL-3.0-or-later
// Streaming monochrome PCL5 and PostScript Level 2. No model-name guessing.
#include "printer_driver.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    printer_io_t io;
    int dpi,paper,w,h,stride,left,top,sw,sh,bpp,y,rows;
    bool ps,open,failed;
    unsigned char *row,*hex;
} standard_job;
static const printer_paper_t papers[]={
    {"A4","iso_a4_210x297mm",21000,29700},{"Letter","na_letter_8.5x11in",21590,27940}
};
static int put(standard_job *j,const void *p,size_t n) {
    if (j->failed) return -1;
    if (j->io.write(j->io.ctx,p,n)<0) j->failed=true;
    return j->failed?-1:0;
}
static int emit(standard_job *j,const char *fmt,...) {
    char s[768]; va_list ap; va_start(ap,fmt); int n=vsnprintf(s,sizeof(s),fmt,ap); va_end(ap);
    if (n<0 || n>=(int)sizeof(s)) { j->failed=true; return -1; }
    return put(j,s,n);
}
static void *begin(const printer_driver_t *d,const printer_io_t *io,int dpi,int paper) {
    if (!io || !io->write || (dpi!=300 && dpi!=600) || paper<0 || paper>=2) return NULL;
    standard_job *j=calloc(1,sizeof(*j)); if (!j) return NULL;
    j->io=*io; j->dpi=dpi; j->paper=paper; j->ps=!strcmp(d->id,"generic-postscript");
    j->left=j->top=dpi/4; // conservative 6.35 mm edge, avoids model-specific imageable areas
    j->w=papers[paper].width*dpi/2540-2*j->left;
    j->h=papers[paper].height*dpi/2540-2*j->top;
    j->stride=(j->w+7)/8; j->row=malloc(j->stride); j->hex=malloc(2*j->stride+1);
    if (!j->row || !j->hex) { free(j->row); free(j->hex); free(j); return NULL; }
    if (j->ps) emit(j,"%%!PS-Adobe-3.0\n%%%%LanguageLevel: 2\n%%%%Pages: (atend)\n");
    else emit(j,"\033%%-12345X@PJL ENTER LANGUAGE=PCL\r\n\033E");
    if (j->failed) { free(j->row); free(j->hex); free(j); return NULL; }
    return j;
}
static int page_begin(void *ctx,int w,int h,int bpp) {
    standard_job *j=ctx;
    if (j->open || j->failed || w<=0 || h<=0 || w>5200 || h>7100 || (bpp!=1 && bpp!=3)) return -1;
    j->sw=w; j->sh=h; j->bpp=bpp; j->y=j->rows=0; j->open=true;
    if (j->ps) {
        // Image matrix maps top-down raster onto a physical page. Polarity: 1=black.
        emit(j,"<< /PageSize [%.6f %.6f] /Duplex false /NumCopies 1 >> setpagedevice\nsave gsave\n"
          "18 %.6f translate\n%.6f %.6f scale\n/DeviceGray setcolorspace\n"
          "<< /ImageType 1 /Width %d /Height %d /BitsPerComponent 1 /Decode [1 0]\n"
          "/ImageMatrix [%d 0 0 -%d 0 %d]\n/DataSource currentfile /ASCIIHexDecode filter >> image\n",
          papers[j->paper].width*72.0/2540,papers[j->paper].height*72.0/2540,
          papers[j->paper].height*72.0/2540-18-j->h*72.0/j->dpi,
          j->w*72.0/j->dpi,j->h*72.0/j->dpi,j->w,j->h,j->w,j->h,j->h);
    } else {
        // Reset page; portrait, simplex, zero top margin, explicit units/position.
        emit(j,"\033&l%dA\033&l0O\033&l0S\033&l0E\033&a0L\033&u%dD"
            "\033*p%dX\033*p%dY\033*t%dR\033*r0F\033*r%dS\033*r%dT\033*b0M\033*r1A",
            j->paper?2:26,j->dpi,j->paper?0:j->dpi/12,j->top,j->dpi,j->w,j->h);
        // PCL logical left boundary is 1/4 inch on Letter, ~1/6 inch on A4.
        // Move to conservative physical 1/4 inch (A4 offset compensates boundary).
        // Position must be set before raster mode, done below via header ordering.
    }
    return j->failed?-1:0;
}
static int output(standard_job *j) {
    if (j->ps) {
        static const char digits[]="0123456789abcdef";
        for (int x=0;x<j->stride;x++) { j->hex[x*2]=digits[j->row[x]>>4]; j->hex[x*2+1]=digits[j->row[x]&15]; }
        j->hex[j->stride*2]='\n'; put(j,j->hex,j->stride*2+1);
    } else { emit(j,"\033*b%dW",j->stride); put(j,j->row,j->stride); }
    j->rows++; return j->failed?-1:0;
}
static int line(void *ctx,int y,const uint8_t *p) {
    static const unsigned char m[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    standard_job *j=ctx;
    if (!j->open || j->failed || y!=j->y || y>=j->sh) return -1;
    j->y++;
    if (y<j->top || y>=j->top+j->h) return 0;
    memset(j->row,0,j->stride);
    if (p) for (int x=0;x<j->w && x+j->left<j->sw;x++) {
        int o=(x+j->left)*j->bpp;
        unsigned g=j->bpp==1?p[o]:(77*p[o]+150*p[o+1]+29*p[o+2])>>8;
        if (g<(unsigned)m[(y-j->top)&3][x&3]*16+8) j->row[x>>3]|=0x80>>(x&7);
    }
    return output(j);
}
static int page_end(void *ctx) {
    standard_job *j=ctx;
    if (!j->open || j->failed || j->y!=j->sh) return -1;
    memset(j->row,0,j->stride);
    while (j->rows<j->h && !j->failed) output(j);
    if (j->ps) emit(j,">\ngrestore\nshowpage restore\n");
    else emit(j,"\033*rB\f");
    j->open=false; return j->failed?-1:0;
}
static int end(void *ctx) {
    standard_job *j=ctx;
    if (j->open) j->failed=true;
    if (!j->failed) emit(j,j->ps?"%%%%Trailer\n%%%%EOF\n\004":"\033E\033%%-12345X");
    int r=j->failed?-1:0;
    if (!r && j->io.flush && j->io.flush(j->io.ctx)<0) r=-1;
    free(j->row); free(j->hex); free(j); return r;
}
static int test(const printer_driver_t *d,const printer_io_t *io,int dpi,const char *label) {
    standard_job *j=begin(d,io,dpi,0); if (!j) return -1;
    int w=papers[0].width*dpi/2540,h=papers[0].height*dpi/2540;
    unsigned char *p=malloc(w); if (!p) { j->failed=true; return end(j); }
    page_begin(j,w,h,1);
    for(int y=0;y<h;y++) {
        memset(p,255,w);
        for(int x=j->left+8;x<w-j->left-8;x++) if(y>=j->top+8 && y<h-j->top-8 &&
            (y<j->top+12 || y>=h-j->top-12 || x<j->left+12 || x>=w-j->left-12)) p[x]=0;
        line(j,y,p); if(io->progress && !(y%64)) io->progress(io->ctx,y,h);
    }
    free(p); int r=page_end(j); if(end(j)<0) r=-1; return r;
}
#define DRIVER(ID,NAME) { .id=ID,.make_model=NAME,.usb_mfg="Generic",.usb_mdl=NAME,.color=false,.scanner=false,\
.resolutions={300,600},.nresolutions=2,.papers=papers,.npapers=2,\
.margin_left=635,.margin_right=635,.margin_top=635,.margin_bottom=635,\
.job_begin=begin,.page_begin=page_begin,.page_line=line,.page_end=page_end,.job_end=end,.test_page=test }
const printer_driver_t generic_pcl5_driver=DRIVER("generic-pcl5","Generic PCL5 monochrome");
const printer_driver_t generic_postscript_driver=DRIVER("generic-postscript","Generic PostScript monochrome");
