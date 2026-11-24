#!/usr/bin/env python3
"""Execute production scan/submission functions with deterministic ESP/RTOS mocks.
No radio, DHCP or printer hardware is emulated by this test.
"""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/wifi.c').read_text()
scan = source[source.index('esp_err_t wifi_scan_networks('):source.index('\nstatic void scan_networks(')]
submit = source[source.index('esp_err_t wifi_submit_setup('):source.index('\nesp_err_t wifi_handover(')]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef struct { int unused; } wifi_ap_record_t;
typedef struct { int unused; } wifi_scan_config_t;
typedef struct { int unused; } network_config_t;
typedef struct { char ssid[33],password[65]; network_config_t network; bool serial; } setup_request_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_WIFI_STATE 3
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define taskENTER_CRITICAL(x) ((void)0)
#define taskEXIT_CRITICAL(x) ((void)0)
#define ESP_LOGW(...) ((void)0)
static bool s_scanning,s_configuration_pending,s_connected,s_has_credentials,s_pending,s_ap,s_initial_credentials;
static unsigned s_force_until;
static int s_setup_queue=1,s_setup_error;
static bool printer_busy,update_busy;
static int disconnects,connects,scans,delays,clears,scan_failures,record_error,queue_success=1;
static setup_request_t queued;
static bool wifi_setup_pending(void){return s_configuration_pending;}
static bool cloud_update_busy(void){return update_busy;}
static bool usb_printer_job_active(void){return printer_busy;}
static unsigned xTaskGetTickCount(void){return 100;}
static int esp_wifi_disconnect(void){disconnects++;return 0;}
static int esp_wifi_connect(void){connects++;return 0;}
static void vTaskDelay(unsigned t){assert(t==200);delays++;}
static int esp_wifi_scan_start(wifi_scan_config_t *s,bool block){assert(block);scans++;return scan_failures-->0?ESP_ERR_WIFI_STATE:ESP_OK;}
static int esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *a){*n=2;return record_error;}
static void esp_wifi_clear_ap_list(void){clears++;}
static int xQueueSend(int q,setup_request_t *r,int wait){queued=*r;return queue_success;}
'''
tests = r'''
static void reset(void){
 s_scanning=s_configuration_pending=s_connected=s_has_credentials=s_pending=s_ap=s_initial_credentials=false;
 s_force_until=0;s_setup_queue=1;s_setup_error=0;
 printer_busy=update_busy=false;disconnects=connects=scans=delays=clears=scan_failures=record_error=0;queue_success=1;
}
int main(void){
 wifi_ap_record_t aps[24];uint16_t count=24;network_config_t cfg={0};
 reset();s_connected=s_has_credentials=true;
 assert(wifi_scan_networks(aps,&count)==ESP_OK);assert(count==2);
 assert(disconnects==0 && connects==0 && scans==1 && !s_scanning);
 reset();s_has_credentials=true;scan_failures=2;
 assert(wifi_scan_networks(aps,&count)==ESP_OK);
 assert(disconnects==1 && connects==1 && scans==3 && delays==2 && !s_scanning);
 reset();s_has_credentials=true;scan_failures=20;
 assert(wifi_scan_networks(aps,&count)==ESP_ERR_WIFI_STATE);
 assert(scans==6 && !s_scanning && connects==1);
 reset();record_error=9;
 assert(wifi_scan_networks(aps,&count)==9);assert(clears==1 && !s_scanning);
 reset();s_configuration_pending=true;
 assert(wifi_scan_networks(aps,&count)==ESP_ERR_INVALID_STATE && scans==0);
 reset();s_connected=s_has_credentials=s_initial_credentials=true;
 assert(wifi_submit_setup("New LAN","password123",&cfg,false)==ESP_OK);
 assert(s_configuration_pending && !strcmp(queued.ssid,"New LAN") && !strcmp(queued.password,"password123"));
 assert(wifi_submit_setup("Other LAN","password123",&cfg,false)==ESP_ERR_INVALID_STATE);
 reset();assert(wifi_submit_setup("LAN","password123",&cfg,false)==ESP_ERR_INVALID_STATE);
 reset();s_ap=true;assert(wifi_submit_setup("LAN","password123",&cfg,false)==ESP_OK);
 reset();s_ap=s_initial_credentials=true;
 assert(wifi_submit_setup("LAN","password123",&cfg,false)==ESP_ERR_INVALID_STATE);
 s_force_until=200;assert(wifi_submit_setup("LAN","password123",&cfg,false)==ESP_OK);
 reset();s_connected=true;printer_busy=true;
 assert(wifi_submit_setup("LAN","password123",&cfg,false)==ESP_ERR_INVALID_STATE);
 reset();s_connected=true;update_busy=true;
 assert(wifi_submit_setup("LAN","password123",&cfg,false)==ESP_ERR_INVALID_STATE);
 reset();s_connected=true;s_scanning=true;
 assert(wifi_submit_setup("LAN","password123",&cfg,false)==ESP_ERR_INVALID_STATE);
 reset();s_connected=true;queue_success=0;
 assert(wifi_submit_setup("LAN","password123",&cfg,false)==ESP_ERR_INVALID_STATE && !s_configuration_pending);
 reset();assert(wifi_submit_setup("LAN","password123",&cfg,true)==ESP_OK);
 reset();s_connected=true;
 assert(wifi_submit_setup("LAN","short",&cfg,false)==ESP_ERR_INVALID_ARG);
 puts("Wi-Fi: connected LAN scan without disconnect, bounded reconnect retries, cleanup, LAN setup, offline/BOOT/serial modes, busy gates and queue rollback passed");
}
'''
with tempfile.TemporaryDirectory() as td:
    path = Path(td)
    (path/'test.c').write_text(harness + scan + submit + tests)
    subprocess.run(['cc','-Wall','-Wextra','-Wno-unused-parameter','-fsanitize=address,undefined','-g',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
# Keep the existing rollback and new LAN response/fallback ordering covered.
worker = source[source.index('static void serial_task('):source.index('esp_err_t wifi_start(')]
assert worker.index('vTaskDelay(pdMS_TO_TICKS(1000))') < worker.index('apply_credentials(s_ssid, s_password)')
assert worker.index('esp_wifi_set_mode(WIFI_MODE_APSTA)') < worker.index('apply_credentials(s_ssid, s_password)')
assert 'apply_credentials(old_ssid, old_password)' in worker
assert worker.index('if (s_pending && s_connected)') < worker.index('stored_credentials(true, s_ssid, s_password)')
print('Worker: response grace, recovery hotspot, retain saved credentials until success and previous-network fallback present')
