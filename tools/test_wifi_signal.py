#!/usr/bin/env python3
"""Compile the actual read-only RSSI route with deterministic platform hooks."""
from pathlib import Path
import subprocess, tempfile, json
root=Path(__file__).resolve().parents[1]
source=(root/'src/network_settings.c').read_text()
a=source.index('    if (!strcmp(path,"/api/wifi-signal")) {'); b=source.index('    bool html=',a)
route=source[a:b]
program=r'''
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef int airprint_http_t;
static bool connected; static int signal_value, reads, sends;
static bool wifi_connected(void) { return connected; }
static int wifi_rssi(void) { reads++; return signal_value; }
static void airprint_http_send_ex(airprint_http_t *h,const char *status,const char *type,const char *headers,const char *data,int n) {
 (void)h;assert(!strcmp(status,"200 OK"));assert(!strcmp(type,"application/json"));
 assert(strstr(headers,"no-store"));assert(n>0 && n<80);sends++;printf("%.*s\n",n,data);
}
static void response(airprint_http_t *h,const char *status,const char *msg,const char *url) {
 (void)h;(void)msg;assert(!strcmp(status,"405 Method Not Allowed"));assert(!url);sends++;
}
static bool route(airprint_http_t *h,const char *method,const char *path) {
'''+route+r'''
return false;
}
int main(void) {
 assert(!route(NULL,"GET","/not-wifi"));assert(!sends && !reads);
 connected=true;signal_value=-71;assert(route(NULL,"GET","/api/wifi-signal"));assert(reads==1);
 connected=false;assert(route(NULL,"GET","/api/wifi-signal"));assert(reads==1);
 connected=true;signal_value=0;assert(route(NULL,"GET","/api/wifi-signal"));assert(reads==2);
 assert(route(NULL,"POST","/api/wifi-signal"));assert(sends==4 && reads==2);
}
'''
with tempfile.TemporaryDirectory(prefix='wifi-signal-') as tmp:
    p=Path(tmp);(p/'route.c').write_text(program)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(p/'route.c'),'-o',str(p/'test')],check=True)
    import os
    lines=subprocess.check_output([str(p/'test')],env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'},text=True).splitlines()
    assert [json.loads(line) for line in lines]==[{'connected':True,'rssi':-71},{'connected':False,'rssi':0},{'connected':True,'rssi':0}]
print('Wi-Fi RSSI route: connected/disconnected/unknown, no-store, GET-only, bounded JSON PASS')
