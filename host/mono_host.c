// SPDX-License-Identifier: GPL-3.0-or-later
#include "../components/printers/mono_raster.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned random_state=123456789;
static unsigned rnd(void) { random_state=1664525u*random_state+1013904223u;return random_state; }
int main(void) {
 uint8_t src[5200*3],got[700],expected[700];
 const unsigned char m[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
 for(int c=0;c<4096;c++) {
  int w=1+rnd()%5100,sw=1+rnd()%5100,bpp=(c&1)?3:1,scale=(c&2)?2:1,left=rnd()%129,y=c&3;
  for(int i=0;i<sw*bpp;i++)src[i]=(c%7==0)?255:(c%7==1)?0:(rnd()>>24);
  memset(got,0,sizeof(got));memset(expected,0,sizeof(expected));
  unsigned ink=0;
  for(int x=0;x<w&&(x+left)/scale<sw;x++) {
   const uint8_t *p=src+((x+left)/scale)*bpp;
   unsigned g=bpp==1?p[0]:(77u*p[0]+150u*p[1]+29u*p[2])>>8;
   if(g<m[y][x&3]*16+8){expected[x/8]|=0x80>>(x&7);ink++;}
  }
  assert(mono_pack(got,w,src,sw,bpp,left,scale==2,y)==ink);
  assert(!memcmp(got,expected,sizeof(got)));
 }
 puts("Mono raster: 4096 byte-exact gray/RGB cases, 300/600 dpi, all dither phases, clipping and partial bytes passed");
}
