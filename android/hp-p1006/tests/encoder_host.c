// SPDX-License-Identifier: GPL-3.0-or-later
#include "encoder.h"
#include <stdlib.h>
int main(int argc,char **argv){if(argc!=2)return 2;int w=1240,h=1748;unsigned char *p=malloc(w*h*4);if(!p)return 2;for(int y=0;y<h;y++)for(int x=0;x<w;x++){unsigned char v=(x<160&&y<160)?(x+y)%256:255;size_t i=((size_t)y*w+x)*4;p[i]=p[i+1]=p[i+2]=v;p[i+3]=255;}vp_encoder *e=vp_begin(argv[1],3);if(!e)return 2;for(int i=0;i<2;i++)if(vp_page(e,p,w,h,w*4))return 3;free(p);return vp_end(e,1)!=0;}
