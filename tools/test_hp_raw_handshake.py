#!/usr/bin/env python3
"""Run the real RAW forwarding loop with a deterministic status-waiting client.
Socket/USB/time are mocked. The 500-ms client deadline is fault injection,
not a measurement of the user's HP driver.
"""
from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[1]
def extract(s):return s[s.index('static ssize_t timed_recv('):s.index('\nstatic void listen_task(')]
pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include "hp_raw_xqx.h"
typedef int esp_err_t;
typedef uint32_t TickType_t;
typedef struct {unsigned vid,pid;} usb_printer_info_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_TIMEOUT 2
#define RAW_IDLE_SECONDS 30
#define RAW_PREAMBLE_SECONDS 60
#define RAW_BUFFER_SIZE 4096
#define RAW_REPLY_POLL_MS 50
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static uint64_t ms,first_reply;
static unsigned poll_ms,received,polls,mode,next_query;
static bool ended_ok,sent_doc,fail_usb;
static unsigned char wire[256];static size_t wire_n;
static void u32(unsigned n){wire[wire_n++]=n>>24;wire[wire_n++]=n>>16;wire[wire_n++]=n>>8;wire[wire_n++]=n;}
static void rec(unsigned n,unsigned count){u32(n);u32(count);}
static void item(void){rec(1,4);u32(0);}
static void init(void){
 ms=first_reply=received=polls=0;poll_ms=0;next_query=13000;ended_ok=sent_doc=fail_usb=false;wire_n=0;
 const char marker[]="\033%-12345X,XQX";memcpy(wire,marker,sizeof(marker)-1);wire_n=sizeof(marker)-1;
 rec(1,1);item();rec(3,1);item();rec(5,1);item();rec(7,1);wire[wire_n++]=0;
 rec(6,0);rec(4,0);rec(2,0);
}
static int mock_setsockopt(int fd,int level,int key,const void *v,size_t n){
 const struct timeval *t=v;
 if(key==SO_RCVTIMEO)poll_ms=t->tv_sec*1000+t->tv_usec/1000;
 return 0;
}
static ssize_t mock_recv(int fd,void *buf,size_t n,int flags){
 if(!received++){memset(buf,'Q',34);memcpy((char *)buf+25,"\033%-12345X",9);return 34;}
 if(first_reply && first_reply<=500 && (mode==0 || mode==3) && !sent_doc){
  assert(n>=wire_n);memcpy(buf,wire,wire_n);sent_doc=true;return mode==3?17:wire_n;
 }
 if(sent_doc && mode!=3)return 0;
 ms+=poll_ms;
 if(mode!=2 && mode!=3 && ms>=next_query){next_query+=13000;memset(buf,'Q',59);memcpy((char *)buf+50,"\033%-12345X",9);return 59;}
 errno=EAGAIN;return -1;
}
static ssize_t mock_send(int fd,const void *b,size_t n,int flags){if(!first_reply)first_reply=ms;return n;}
#define setsockopt mock_setsockopt
#define recv mock_recv
#define send mock_send
static int64_t esp_timer_get_time(void){return ms*1000;}
static TickType_t xTaskGetTickCount(void){return ms;}
static bool usb_printer_get_info(usb_printer_info_t *p){p->vid=0x03f0;p->pid=0x3e17;return true;}
static int usb_printer_job_begin(void){return 0;}
static int usb_printer_raw_write(const uint8_t *b,size_t n){return fail_usb?ESP_FAIL:ESP_OK;}
static int usb_printer_raw_flush(void){return 0;}
static int usb_printer_raw_poll_reply(uint8_t *b,size_t n){if(polls++==0)return 0;memset(b,'R',16);return 16;}
static void usb_printer_job_end(bool ok){ended_ok=ok;}
static void status_led_set_raw_printing(bool b){}
'''
post=r'''
int main(void){
 uint8_t buffer[4096];
#ifdef RAWFIX1
 init();mode=1;forward_job(1,buffer);assert(first_reply==100 && !ended_ok);
 puts("RAWFIX1: UEL control escape mistaken for partial XQX; still resets after status timeout");
 return 0;
#endif
 init();mode=0;forward_job(1,buffer);
#ifdef BASELINE
 assert(first_reply==2000 && !sent_doc && !ended_ok);
 printf("baseline: first status reply %llu ms, client withheld XQX, firmware reset after %llu ms\n",(unsigned long long)first_reply,(unsigned long long)ms);
#else
 assert(first_reply==100 && sent_doc && ended_ok);
 printf("fixed: first status reply %llu ms, XQX completed, no reset\n",(unsigned long long)first_reply);
 init();mode=1;forward_job(1,buffer);assert(!sent_doc && ended_ok && ms>=60000);
 puts("status-only session: bounded timeout releases printer without reset");
 init();mode=2;forward_job(1,buffer);assert(!sent_doc && ended_ok && ms>=30000);
 puts("idle control-only session also releases without resetting HP");
 init();mode=3;forward_job(1,buffer);assert(sent_doc && !ended_ok);
 puts("timeout after partial XQX still aborts and requests recovery");
 init();mode=0;fail_usb=true;forward_job(1,buffer);assert(!ended_ok);
 puts("genuine USB write failure still aborts and requests recovery");
#endif
}
'''
base=subprocess.check_output(['git','show','3a9b72b:src/hp_raw_bridge.c'],cwd=r,text=True)
rawfix1=subprocess.check_output(['git','show','595626d192bdd45d1d7c3f92a331b668ff70eee8:src/hp_raw_bridge.c'],cwd=r,text=True)
current=(r/'src/hp_raw_bridge.c').read_text()
with tempfile.TemporaryDirectory() as td:
 for label,s in [('baseline',base),('rawfix1',rawfix1),('fixed',current)]:
  p=Path(td)/label;p.with_suffix('.c').write_text(pre+extract(s)+post)
  flags=['-DBASELINE'] if label=='baseline' else ['-DRAWFIX1'] if label=='rawfix1' else []
  subprocess.run(['cc','-g','-Wall','-Wextra','-Wno-unused-parameter','-fsanitize=address,undefined',*flags,'-I'+str(r/'src'),str(p.with_suffix('.c')),'-o',str(p)],check=True)
  subprocess.run([str(p)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
