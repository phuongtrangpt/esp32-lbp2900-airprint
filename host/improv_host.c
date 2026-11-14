// SPDX-License-Identifier: GPL-3.0-or-later
#include "improv_serial.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t out[4096]; static size_t out_n; static unsigned commands;
static void write_frame(void *ctx,const uint8_t *p,size_t n) { assert(out_n+n<=sizeof(out)); memcpy(out+out_n,p,n);out_n+=n; }
static void command(void *ctx,uint8_t cmd,const uint8_t *data,size_t n)
{
    commands++;
    if(cmd==2) improv_serial_state(ctx,2);
    else if(cmd==3) { const char *str[]={"LBP2900 AirPrint","0.2.0-web-preview","ESP32-S3 N16R8","Canon LBP2900"}; assert(improv_serial_result(ctx,3,str,4)); }
    else if(cmd==1) { char ssid[33],pass[65]; assert(improv_serial_wifi(data,n,ssid,pass)); assert(!strcmp(ssid,"MyWiFi") && !strcmp(pass,"secret12")); }
    else improv_serial_error(ctx,2);
}
static size_t packet(uint8_t *p,uint8_t cmd,const uint8_t *data,size_t n)
{
    memcpy(p,"IMPROV",6);p[6]=1;p[7]=3;p[8]=n+2;p[9]=cmd;p[10]=n;
    if(n) memcpy(p+11,data,n);
    uint8_t sum=0; for(size_t i=0;i<n+11;i++) sum+=p[i];p[n+11]=sum;return n+12;
}
static void verify_output(void)
{
    for(size_t pos=0;pos<out_n;) {
        assert(out_n-pos>=10 && !memcmp(out+pos,"IMPROV",6));
        size_t n=out[pos+8]+10;assert(pos+n<=out_n);
        uint8_t sum=0;for(size_t i=0;i<n-1;i++) sum+=out[pos+i];assert(sum==out[pos+n-1]);
        pos+=n;
    }
}
int main(void)
{
    improv_serial_t s={.write=write_frame,.command=command};s.ctx=&s;
    uint8_t frame[265];size_t n=packet(frame,2,NULL,0);
    const char *noise="boot log: IIMPRO no secrets\n";
    for(size_t i=0;i<strlen(noise);i++) improv_serial_feed(&s,noise[i]);
    for(size_t i=0;i<n;i++) improv_serial_feed(&s,frame[i]);
    assert(commands==1 && out_n==22 && out[7]==2 && out[9]==0 && out[20]==2);
    verify_output();out_n=0;
    n=packet(frame,3,NULL,0);for(size_t i=0;i<n;i++) improv_serial_feed(&s,frame[i]);
    assert(commands==2 && out[18]==4);verify_output();out_n=0;
    uint8_t credentials[]={6,'M','y','W','i','F','i',8,'s','e','c','r','e','t','1','2'};
    n=packet(frame,1,credentials,sizeof(credentials));
    for(size_t i=0;i<n;i++) { improv_serial_feed(&s,frame[i]); }
    assert(commands==3);verify_output();out_n=0;
    frame[n-1]^=1;for(size_t i=0;i<n;i++) improv_serial_feed(&s,frame[i]);
    assert(commands==3 && out_n==11 && out[7]==2 && out[9]==1);verify_output();out_n=0;
    n=packet(frame,2,NULL,0);frame[10]=1;frame[n-1]++;
    for(size_t i=0;i<n;i++) { improv_serial_feed(&s,frame[i]); }
    assert(commands==3 && out[9]==1);out_n=0;
    char ssid[33],pass[65];
    for(size_t len=0;len<sizeof(credentials);len++) assert(!improv_serial_wifi(credentials,len,ssid,pass));
    assert(improv_serial_wifi(credentials,sizeof(credentials),ssid,pass));
    credentials[8]=0;assert(!improv_serial_wifi(credentials,sizeof(credentials),ssid,pass));
    uint8_t open[]={1,'x',0};assert(improv_serial_wifi(open,sizeof(open),ssid,pass) && !pass[0]);
    // Every possible packet length must remain bounded; most are invalid RPC.
    s.command=NULL;
    for(unsigned len=0;len<256;len++) {
        memcpy(frame,"IMPROV",6);frame[6]=1;frame[7]=3;frame[8]=len;
        memset(frame+9,0,len);if(len>=2)frame[10]=255;uint8_t sum=0;
        for(unsigned i=0;i<len+9;i++) { sum+=frame[i]; }
        frame[len+9]=sum;
        for(unsigned i=0;i<len+10;i++) { improv_serial_feed(&s,frame[i]); }
        out_n=0;
    }
    puts("Improv Serial: PASS: fragmented frames, checksums, noise/resync, RPC lengths, Wi-Fi bounds, open networks, all 256 packet sizes");
}
