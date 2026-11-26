// SPDX-License-Identifier: GPL-3.0-or-later
#include "hp_pc_bridge.h"
#include "hp_offload.h"
#include "usb_printer.h"
#include "wifi.h"
#include "cloud_update.h"
#include "status_led.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <stdlib.h>
static const char *TAG="hp_pc";
static int ready(void *ctx) {
    (void)ctx;usb_printer_info_t info;
    if(!usb_printer_get_info(&info) || info.vid!=0x03f0 || info.pid!=0x3e17)return HP_PC_MODEL;
    if(usb_printer_job_active() || cloud_update_busy() || wifi_setup_pending())return HP_PC_BUSY;
    return 0;
}
static int begin(void *ctx) { (void)ctx;if(usb_printer_job_begin()!=ESP_OK)return HP_PC_BUSY;status_led_set_raw_printing(true);return 0; }
static int write_usb(void *ctx,const uint8_t *p,size_t n) { (void)ctx;return usb_printer_pc_write(p,n)==ESP_OK?0:-1; }
static int flush(void *ctx) { (void)ctx;return usb_printer_pc_flush()==ESP_OK?0:-1; }
static int poll_reply(void *ctx) {
    (void)ctx;uint8_t b[512];
    // Bounded nonblocking drain. No document data is logged.
    for(int i=0;i<8;i++){int n=usb_printer_raw_poll_reply(b,sizeof(b));if(n<0)return -1;if(!n)break;}
    return 0;
}
static void end(void *ctx,bool ok) { (void)ctx;usb_printer_job_end(ok);status_led_set_raw_printing(false); }
static void task(void *arg) {
    uint8_t *buffer=arg;hp_offload_state_t state={0};
    const hp_offload_io_t io={.ready=ready,.begin=begin,.write=write_usb,.flush=flush,.poll=poll_reply,.end=end};
    for(;;) {
        char ip[16];if(!wifi_get_ip(ip,sizeof(ip))){vTaskDelay(pdMS_TO_TICKS(1000));continue;}
        struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(HP_OFFLOAD_PORT)};
        if(inet_pton(AF_INET,ip,&addr.sin_addr)!=1){vTaskDelay(pdMS_TO_TICKS(1000));continue;}
        int listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(listener>=0) {
            int reuse=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
            if(bind(listener,(struct sockaddr*)&addr,sizeof(addr))==0 && listen(listener,1)==0) {
                ESP_LOGI(TAG,"Print Connect listening at %s:%d; no raster/JBIG on this path",ip,HP_OFFLOAD_PORT);
                for(;;) {
                    char current[16];if(!wifi_get_ip(current,sizeof(current)) || inet_addr(current)!=addr.sin_addr.s_addr)break;
                    fd_set fds;FD_ZERO(&fds);FD_SET(listener,&fds);struct timeval timeout={.tv_sec=1};
                    int n=select(listener+1,&fds,NULL,NULL,&timeout);if(n<0)break;if(!n)continue;
                    int fd=accept(listener,NULL,NULL);if(fd<0)break;
                    int status=hp_offload_session(fd,&io,&state,buffer);close(fd);
                    if(status!=HP_PC_READY) ESP_LOGI(TAG,"PC session status=%d accepted=%llu/%llu bytes; USB completion does not confirm paper output",status,(unsigned long long)state.accepted,(unsigned long long)state.total);
                }
            }
            close(listener);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
esp_err_t hp_pc_bridge_start(void) {
    uint8_t *buffer=heap_caps_malloc(HP_OFFLOAD_CHUNK,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!buffer)return ESP_ERR_NO_MEM;
    if(xTaskCreate(task,"hp_pc",4096,buffer,4,NULL)!=pdPASS){free(buffer);return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
