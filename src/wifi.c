#include "wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_app_desc.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "improv_serial.h"
#include "local_config.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG="wifi_sta";
static volatile bool s_connected, s_has_credentials;
static int s_retries;
static esp_netif_t *s_netif;
static improv_serial_t s_improv;
// Only the serial task owns provisioning data and emits Improv packets.
static bool s_pending;
static TickType_t s_started;
static char s_ssid[33],s_password[65];
static bool s_legacy_password;
static char s_legacy_ssid[33];

static void on_event(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START) {
        if(s_has_credentials) esp_wifi_connect();
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d=data;
        s_connected=false;
        if(s_has_credentials) {
            if(s_retries++%10==0) ESP_LOGW(TAG,"disconnected (reason %d), retrying",d->reason);
            esp_wifi_connect();
        }
    } else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e=data;
        ESP_LOGI(TAG,"connected, IP " IPSTR,IP2STR(&e->ip_info.ip));
        s_retries=0; s_connected=true;
    }
}
static esp_err_t stored_credentials(bool save,char ssid[33],char password[65])
{
    nvs_handle_t h;
    esp_err_t e=nvs_open("lbp2900",save?NVS_READWRITE:NVS_READONLY,&h);
    if(e!=ESP_OK) return e;
    if(save) {
        e=nvs_set_str(h,"ssid",ssid);
        if(e==ESP_OK) e=nvs_set_str(h,"password",password);
        if(e==ESP_OK) e=nvs_commit(h);
    } else {
        size_t a=33,b=65;
        e=nvs_get_str(h,"ssid",ssid,&a);
        if(e==ESP_OK) e=nvs_get_str(h,"password",password,&b);
    }
    nvs_close(h); return e;
}
static void serial_write(void *ctx,const uint8_t *p,size_t n)
{
    // Raw UART output preserves binary bytes (no CR/LF translation). Keep a
    // complete frame together relative to normal stdout/ESP log messages.
    flockfile(stdout);
    uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM,p,n);
    funlockfile(stdout);
}
static uint8_t provision_state(void) { return s_connected?4:(s_pending?3:2); }
static void result_url(uint8_t command)
{
    char ip[24],url[48];
    if(wifi_get_ip(ip,sizeof(ip))) {
        snprintf(url,sizeof(url),"http://%s/",ip);
        const char *strings[]={url}; improv_serial_result(&s_improv,command,strings,1);
    } else improv_serial_result(&s_improv,command,NULL,0);
}
static esp_err_t apply_credentials(const char *ssid,const char *password)
{
    s_has_credentials=false; s_connected=false;
    esp_wifi_disconnect();
    wifi_config_t cfg={0};
    memcpy(cfg.sta.ssid,ssid,strlen(ssid));
    memcpy(cfg.sta.password,password,strlen(password));
    cfg.sta.threshold.authmode=password[0]?WIFI_AUTH_WPA_PSK:WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable=true;
    esp_err_t e=esp_wifi_set_config(WIFI_IF_STA,&cfg);
    if(e==ESP_OK) { s_has_credentials=true; e=esp_wifi_connect(); }
    return e;
}
static void scan_networks(void)
{
    if(s_pending) { improv_serial_result(&s_improv,4,NULL,0); return; }
    wifi_scan_config_t scan={0};
    if(esp_wifi_scan_start(&scan,true)==ESP_OK) {
        uint16_t count=16;
        wifi_ap_record_t *aps=calloc(count,sizeof(*aps));
        if(aps && esp_wifi_scan_get_ap_records(&count,aps)==ESP_OK) {
            for(unsigned i=0;i<count;i++) {
                if(!aps[i].ssid[0]) continue;
                char signal[12]; snprintf(signal,sizeof(signal),"%d",aps[i].rssi);
                const char *strings[]={(char *)aps[i].ssid,signal,aps[i].authmode==WIFI_AUTH_OPEN?"NO":"YES"};
                improv_serial_result(&s_improv,4,strings,3);
            }
        }
        free(aps);
    }
    improv_serial_result(&s_improv,4,NULL,0);
}
static void command(void *ctx,uint8_t cmd,const uint8_t *p,size_t n)
{
    if(cmd==1) {
        char ssid[33],password[65];
        if(s_pending || !improv_serial_wifi(p,n,ssid,password)) { improv_serial_error(&s_improv,1); return; }
        memcpy(s_ssid,ssid,sizeof(ssid)); memcpy(s_password,password,sizeof(password));
        s_pending=true; s_started=xTaskGetTickCount();
        improv_serial_state(&s_improv,3);
        if(apply_credentials(s_ssid,s_password)!=ESP_OK) {
            s_pending=false; improv_serial_error(&s_improv,3); improv_serial_state(&s_improv,2);
        }
        memset(password,0,sizeof(password));
    } else if(n) improv_serial_error(&s_improv,1);
    else if(cmd==2) {
        improv_serial_state(&s_improv,provision_state());
        if(s_connected) result_url(2);
    } else if(cmd==3) {
        const char *strings[]={"LBP2900 AirPrint",esp_app_get_description()->version,"ESP32-S3 N16R8",PRINTER_NAME};
        improv_serial_result(&s_improv,3,strings,4);
    } else if(cmd==4) scan_networks();
    else improv_serial_error(&s_improv,2);
}
static void legacy_line(const char *line)
{
    // Keep the old serial setup available. "setup" restarts its two prompts.
    if(!strcmp(line,"setup")) { s_has_credentials=false; s_connected=false; esp_wifi_disconnect(); s_legacy_password=false; s_legacy_ssid[0]=0; puts("WiFi SSID:"); return; }
    if(s_pending || s_has_credentials) return;
    size_t n=strlen(line);
    if(!s_legacy_password) {
        if(n==0 || n>32) { puts("WiFi SSID: 1-32 UTF-8 bytes"); return; }
        memcpy(s_legacy_ssid,line,n+1); s_legacy_password=true;
        puts("WiFi password (8-63 characters, or empty):");
    } else {
        if(n>63 || (n && n<8)) { puts("WiFi password: 8-63 characters, or empty"); return; }
        uint8_t data[98]; size_t a=strlen(s_legacy_ssid);
        data[0]=a; memcpy(data+1,s_legacy_ssid,a); data[a+1]=n; memcpy(data+a+2,line,n);
        command(NULL,1,data,a+n+2);
        memset(data,0,sizeof(data)); s_legacy_password=false;
    }
}
static void serial_task(void *arg)
{
    uint8_t byte; char line[100]={0}; size_t used=0; bool overflow=false;
    TickType_t last=xTaskGetTickCount();
    if(!s_has_credentials) puts("LBP2900: open the web installer to set WiFi, or enter SSID then password here.");
    for(;;) {
        if(uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM,&byte,1,pdMS_TO_TICKS(20))==1) {
            TickType_t now=xTaskGetTickCount();
            if(now-last>pdMS_TO_TICKS(1000)) s_improv.used=0;
            last=now;
            bool frame=improv_serial_feed(&s_improv,byte);
            if(frame) { used=0; overflow=false; memset(line,0,sizeof(line)); }
            else if(byte=='\n' && !s_improv.used) {
                if(!overflow) { line[used]=0; legacy_line(line); }
                used=0; overflow=false; memset(line,0,sizeof(line));
            } else if(byte==8 || byte==127) { if(used) used--; }
            else if(byte>=32) { if(used+1<sizeof(line)) line[used++]=(char)byte; else overflow=true; }
        }
        if(s_pending && s_connected) {
            if(stored_credentials(true,s_ssid,s_password)!=ESP_OK) improv_serial_error(&s_improv,0xff);
            else improv_serial_error(&s_improv,0);
            s_pending=false; memset(s_password,0,sizeof(s_password));
            improv_serial_state(&s_improv,4); result_url(1);
        } else if(s_pending && xTaskGetTickCount()-s_started>pdMS_TO_TICKS(30000)) {
            s_pending=false; s_has_credentials=false; esp_wifi_disconnect();
            memset(s_password,0,sizeof(s_password));
            improv_serial_error(&s_improv,3); improv_serial_state(&s_improv,2);
        }
    }
}
esp_err_t wifi_start(const char *hostname)
{
    esp_err_t err=nvs_flash_init();
    if(err==ESP_ERR_NVS_NO_FREE_PAGES || err==ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(),TAG,"erase NVS"); err=nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err,TAG,"NVS");
    char ssid[33]={0},password[65]={0};
    if(stored_credentials(false,ssid,password)!=ESP_OK) {
        snprintf(ssid,sizeof(ssid),"%s",WIFI_SSID);
        snprintf(password,sizeof(password),"%s",WIFI_PASSWORD);
    }
    s_has_credentials=ssid[0]!=0;
    ESP_RETURN_ON_ERROR(esp_netif_init(),TAG,"netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(),TAG,"event loop");
    s_netif=esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_netif,hostname);
    esp_log_level_set("wifi",ESP_LOG_WARN); esp_log_level_set("wifi_init",ESP_LOG_WARN);
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init),TAG,"WiFi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,on_event,NULL),TAG,"WiFi event");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,on_event,NULL),TAG,"IP event");
    wifi_config_t cfg={0};
    memcpy(cfg.sta.ssid,ssid,strlen(ssid)); memcpy(cfg.sta.password,password,strlen(password));
    cfg.sta.threshold.authmode=password[0]?WIFI_AUTH_WPA_PSK:WIFI_AUTH_OPEN; cfg.sta.pmf_cfg.capable=true;
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM),TAG,"WiFi storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA),TAG,"WiFi mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA,&cfg),TAG,"WiFi config");
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_RETURN_ON_ERROR(esp_wifi_start(),TAG,"WiFi start");
    memset(password,0,sizeof(password));
    if(!uart_is_driver_installed(CONFIG_ESP_CONSOLE_UART_NUM))
        ESP_RETURN_ON_ERROR(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM,1024,0,0,NULL,0),TAG,"serial provisioning");
    s_improv=(improv_serial_t){.write=serial_write,.command=command};
    if(xTaskCreate(serial_task,"improv",4096,NULL,3,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
    esp_sntp_config_t sntp=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org"); esp_netif_sntp_init(&sntp);
    ESP_LOGI(TAG,"WiFi station %s; browser provisioning ready",hostname);
    return ESP_OK;
}
bool wifi_connected(void) { return s_connected; }
bool wifi_get_ip(char *buf,size_t n)
{
    esp_netif_ip_info_t ip;
    if(!s_connected || !s_netif || esp_netif_get_ip_info(s_netif,&ip)!=ESP_OK) { if(n) buf[0]=0; return false; }
    snprintf(buf,n,IPSTR,IP2STR(&ip.ip)); return true;
}
int wifi_rssi(void)
{
    int rssi=0;
    return s_connected && esp_wifi_sta_get_rssi(&rssi)==ESP_OK?rssi:0;
}
