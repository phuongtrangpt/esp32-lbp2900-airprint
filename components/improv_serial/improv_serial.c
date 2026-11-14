// SPDX-License-Identifier: GPL-3.0-or-later
// Improv Serial v1 framing: https://www.improv-wifi.com/serial/
#include "improv_serial.h"
#include <string.h>
static void send(improv_serial_t *s, uint8_t type, const uint8_t *data, size_t len)
{
    uint8_t frame[265]={'I','M','P','R','O','V',1,type,(uint8_t)len};
    memcpy(frame+9,data,len);
    uint8_t sum=0;
    for(size_t i=0;i<len+9;i++) sum+=frame[i];
    frame[len+9]=sum;
    s->write(s->ctx,frame,len+10);
}
void improv_serial_state(improv_serial_t *s,uint8_t v) { send(s,1,&v,1); }
void improv_serial_error(improv_serial_t *s,uint8_t v) { send(s,2,&v,1); }
bool improv_serial_result(improv_serial_t *s,uint8_t cmd,const char *const *str,size_t count)
{
    uint8_t body[255]={cmd,0}; size_t n=2;
    for(size_t i=0;i<count;i++) {
        size_t len=strlen(str[i]);
        if(len>252 || n+len+1>sizeof(body)) return false;
        body[n++]=(uint8_t)len; memcpy(body+n,str[i],len); n+=len;
    }
    body[1]=(uint8_t)(n-2); send(s,4,body,n); return true;
}
bool improv_serial_wifi(const uint8_t *p,size_t n,char ssid[33],char password[65])
{
    memset(ssid,0,33); memset(password,0,65);
    if(n<2 || p[0]==0 || p[0]>32 || (size_t)p[0]+2>n) return false;
    size_t a=p[0],b=p[a+1];
    if(b>63 || (b && b<8) || a+b+2!=n || memchr(p+1,0,a) || memchr(p+a+2,0,b)) return false;
    memcpy(ssid,p+1,a); memcpy(password,p+a+2,b); return true;
}
bool improv_serial_feed(improv_serial_t *s,uint8_t b)
{
    static const uint8_t header[]="IMPROV";
    if(s->used<6 && b!=header[s->used]) { s->used=b=='I'?1:0; return false; }
    s->packet[s->used++]=b;
    if(s->used==7 && b!=1) { s->used=0; return true; }
    if(s->used<9) return false;
    size_t size=(size_t)s->packet[8]+10;
    if(s->used<size) return false;
    s->used=0;
    uint8_t sum=0; for(size_t i=0;i<size-1;i++) sum+=s->packet[i];
    const uint8_t *p=s->packet+9; size_t n=s->packet[8];
    if(sum!=b || s->packet[7]!=3 || n<2 || p[1]!=n-2) { improv_serial_error(s,1); return true; }
    improv_serial_error(s,0);
    s->command(s->ctx,p[0],p+2,n-2);
    return true;
}
