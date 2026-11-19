// SPDX-License-Identifier: MIT
// C port of juliusneu/lbp6030-printer-app bandcodec.py, commit 2ce3542892db.
// Copyright (c) 2026 juliusneu; adaptations for vietphuong esp, 2026.
// See LICENSE in this directory. No Canon binaries or code.
#include "lbp6030.h"
#include <string.h>
#include <limits.h>
typedef struct { uint8_t *out; size_t n,cap; unsigned bits,value; int failed; } bits_t;
static void bits(bits_t *b, unsigned v, unsigned n) {
    while (n--) {
        b->value=(b->value<<1)|((v>>n)&1); b->bits++;
        if(b->bits==8) { if(b->n==b->cap) b->failed=1; else b->out[b->n++]=(uint8_t)b->value^0x43; b->bits=b->value=0; }
    }
}
static void number(bits_t *b,unsigned v) {
    if(!v) bits(b,63,6);
    else if(v==1) bits(b,0,2);
    else if(v<4) bits(b,v==2?3:2,3);
    else { unsigned k=0,t=v; while(t>>=1) k++; k-=1;
        bits(b,((1u<<k)-1)<<1,k+1); bits(b,(1u<<(k+2))-1-v,k+1); }
}
static void pad(bits_t *b) { unsigned k=(32-((b->n*8+b->bits)%32))%32; if(k) bits(b,(1u<<k)-1,k); }
size_t lbp6030_encode(const uint8_t *raw,size_t stride,size_t rows,int last,uint8_t *out,size_t cap) {
    if(!raw || !out || !stride || !rows || stride>8192 || rows>256 || rows>SIZE_MAX/stride) return 0;
    size_t total=stride*rows,p=0; int stash[16],ns=0;
    size_t dist[5]={3*stride+2,stride,8,80,4};
    static const unsigned op[]={0,12,14,30,62},nb[]={1,4,4,5,6};
    bits_t b={.out=out,.cap=cap};
    while(p<total && !b.failed) {
        size_t best=0,cmd=0,limit=stride-p%stride;
        if(limit>16383) limit=16383;
        for(size_t c=0;c<5;c++) {
            size_t len=0,d=dist[c];
            if(d && p>=d) while(len<limit && raw[p+len]==raw[p+len-d]) len++;
            if(len>best) {best=len;cmd=c;}
        }
        if(best>=2) {
            if(best/128) {bits(&b,252,8);number(&b,(unsigned)(best/128));}
            bits(&b,op[cmd],nb[cmd]);number(&b,(unsigned)(best%128));
            if(cmd==1) {size_t t=dist[0];dist[0]=dist[1];dist[1]=t;}
            if(cmd==4) {size_t t=dist[2];dist[2]=dist[4];dist[4]=t;}
            p+=best;
        } else {
            int v=raw[p++],i=0; while(i<ns && stash[i]!=v) i++;
            if(i<ns) {bits(&b,2,2);bits(&b,15-i,4);}
            else if(!v) bits(&b,253,8);
            else {bits(&b,13,4);bits(&b,(unsigned)v,8);}
            if(i>=ns) {if(ns<16) ns++;i=ns-1;}
            for(;i>0;i--) { stash[i]=stash[i-1]; }
            stash[0]=v;
        }
    }
    bits(&b,254,8);bits(&b,0,2);pad(&b);
    if(last) {bits(&b,254,8);bits(&b,1,2);pad(&b);}
    return b.failed?0:b.n;
}
