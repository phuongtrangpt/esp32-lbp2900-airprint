#!/usr/bin/env python3
"""Exercise the ESP transport yield/deadline path with a deterministic clock.
USB hardware/FreeRTOS scheduling is still validated on a real board.
"""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as td:
    d=Path(td); (d/'freertos').mkdir()
    (d/'freertos/FreeRTOS.h').write_text('#pragma once\n')
    (d/'freertos/task.h').write_text('void vTaskDelay(unsigned);\n')
    (d/'esp_log.h').write_text('#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGD(...) ((void)0)\n')
    (d/'test.c').write_text(r'''
#include "capt_transport.h"
#include "capt-command.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
static unsigned long long ms;
static unsigned yields;
static unsigned char command[2];
int mock_clock(clockid_t id, struct timespec *t) { (void)id; t->tv_sec=ms/1000; t->tv_nsec=(ms%1000)*1000000; return 0; }
void vTaskDelay(unsigned n) { assert(n==1); yields++; ms+=10; }
static int wr(void *c,const unsigned char *b,size_t n) { (void)c; assert(n==4);command[0]=b[0];command[1]=b[1];ms++;return 0; }
static int flush(void *c) { (void)c;return 0; }
static int rd(void *c,unsigned char *b,size_t n,unsigned timeout) { (void)c;assert(n==6&&timeout>0);b[0]=command[0];b[1]=command[1];b[2]=6;b[3]=b[4]=b[5]=0;return 6; }
int main(void) {
 static capt_transport_t t; t.io=(printer_io_t){.write=wr,.flush=flush,.read=rd};t.buffer=malloc(65536);assert(t.buffer);capt_bind(&t);
 assert(!setjmp(t.guard));capt_deadline(&t,120000);
 for(int i=0;i<1000;i++) { unsigned char b[2];size_t n=2;capt_sendrecv(0xa1a1,NULL,0,b,&n);assert(n==2); }
 assert(yields>0&&yields<100); // watchdog is serviced without a forced tick per command/read
 printf("ESP CAPT scheduler: 1000 replies, %u bounded watchdog yields; ",yields);
 ms=t.deadline_ms+1;
 if(!setjmp(t.guard)) { capt_sendrecv(0xa1a1,NULL,0,NULL,NULL);assert(0); }
 assert(t.error);free(t.buffer);puts("expired deadline still aborts");
}
''')
    subprocess.run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-DESP_PLATFORM','-Dclock_gettime=mock_clock','-I'+str(d),'-I'+str(root/'components/capt/include'),'-I'+str(root/'components/capt/vendor'),'-I'+str(root/'components/airprint/include'),str(d/'test.c'),str(root/'components/capt/capt_transport.c'),'-o',str(d/'test')],check=True)
    subprocess.run([str(d/'test')],check=True)
