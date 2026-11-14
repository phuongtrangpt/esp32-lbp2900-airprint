// SPDX-License-Identifier: GPL-3.0-or-later
// Optional, user-supplied HP firmware: deliberately not redistributed.
#include "hp_firmware.h"
#include "usb_printer.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#define MAX_IMAGE (256*1024)
#define MAGIC 0x36504846u
static portMUX_TYPE init_lock=portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t mutex;
static SemaphoreHandle_t lock(void) {
    SemaphoreHandle_t candidate=xSemaphoreCreateMutex();
    taskENTER_CRITICAL(&init_lock);
    if (!mutex) { mutex=candidate; candidate=NULL; }
    SemaphoreHandle_t m=mutex;
    taskEXIT_CRITICAL(&init_lock);
    if (candidate) vSemaphoreDelete(candidate);
    return m;
}
static const esp_partition_t *partition(void) {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"hp_firmware");
}
static const char page[]=
"<!doctype html><html lang=vi><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>HP P1006 firmware</title><style>body{font:18px system-ui;max-width:680px;margin:40px auto;padding:20px}button,input{font:inherit;margin:12px 0}a{color:teal}</style>"
"<h1>HP LaserJet P1006</h1><p>Chọn file <b>sihpP1006.dl</b> từ bộ driver của bạn. ESP lưu một lần và gửi lại vào máy in khi bắt đầu in sau mỗi lần kết nối USB.</p>"
"<p>Không dùng firmware ESP (.bin), file .img hay file của máy in khác. Bản thử nghiệm chưa được kiểm chứng trên máy in thật.</p>"
"<input type=file id=f accept=.dl><br><button id=b>Lưu firmware HP</button><p id=s></p><a href='/'>Về trang in</a>"
"<script>b.onclick=async()=>{if(!f.files.length)return;s.textContent='Đang lưu…';b.disabled=true;try{let r=await fetch('/hp-firmware',{method:'POST',body:f.files[0]});s.textContent=await r.text()}catch(e){s.textContent=e.message}finally{b.disabled=false}}</script></html>";
bool hp_firmware_http(airprint_http_t *h,const char *method,const char *path) {
    if (strcmp(path,"/hp-firmware")) return false;
    if (!strcmp(method,"GET")) { airprint_http_send(h,"200 OK","text/html; charset=utf-8",page,sizeof(page)-1); return true; }
    const char *msg="Chỉ hỗ trợ GET hoặc POST."; const char *status="405 Method Not Allowed";
    if (!strcmp(method,"POST")) {
        SemaphoreHandle_t m=lock();
        if (!m || xSemaphoreTake(m,0)!=pdTRUE) { msg="Đang sử dụng firmware, thử lại sau."; status="409 Conflict"; }
        else {
            const esp_partition_t *p=partition(); unsigned char *buf=malloc(MAX_IMAGE+1); size_t n=0; int got=0;
            if (p && buf) {
                while(n<=MAX_IMAGE && (got=airprint_http_read_body(h,buf+n,MAX_IMAGE+1-n))>0) n+=got;
                bool valid=got==0 && n>=32 && n<=MAX_IMAGE &&
                    ((buf[0]==0xbe && buf[1]==0xef) || !memcmp(buf,"\033%-12345X",9));
                if (valid) {
                    uint32_t hdr[]={MAGIC,n,esp_rom_crc32_le(0,buf,n)};
                    if (esp_partition_erase_range(p,0,p->size)==ESP_OK &&
                        esp_partition_write(p,sizeof(hdr),buf,n)==ESP_OK &&
                        esp_partition_write(p,0,hdr,sizeof(hdr))==ESP_OK) {
                        msg="Đã lưu. Tắt/bật máy in rồi kết nối lại USB với ESP trước khi in thử."; status="200 OK";
                    } else { msg="Không ghi được bộ nhớ flash."; status="500 Internal Server Error"; }
                } else { msg="File HPDL không hợp lệ hoặc quá 256 KB. Chọn đúng sihpP1006.dl."; status="400 Bad Request"; }
            } else { msg="Không đủ bộ nhớ hoặc chưa có phân vùng HP. Nạp lại firmware ESP qua web."; status="503 Service Unavailable"; }
            free(buf); xSemaphoreGive(m);
        }
    }
    airprint_http_send(h,status,"text/plain; charset=utf-8",msg,strlen(msg)); return true;
}
esp_err_t hp_firmware_load(void) {
    SemaphoreHandle_t m=lock(); if (!m || xSemaphoreTake(m,pdMS_TO_TICKS(1000))!=pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_partition_t *p=partition(); uint32_t hdr[3]; esp_err_t rc=ESP_FAIL;
    unsigned char *buf=NULL;
    if (!p || esp_partition_read(p,0,hdr,sizeof(hdr))!=ESP_OK || hdr[0]!=MAGIC || hdr[1]<32 || hdr[1]>MAX_IMAGE) goto done;
    buf=malloc(hdr[1]); if (!buf) goto done;
    if (esp_partition_read(p,sizeof(hdr),buf,hdr[1])!=ESP_OK || esp_rom_crc32_le(0,buf,hdr[1])!=hdr[2]) goto done;
    rc=usb_printer_write(buf,hdr[1]); if (rc==ESP_OK) rc=usb_printer_flush();
    if (rc==ESP_OK) vTaskDelay(pdMS_TO_TICKS(6000));
done:
    free(buf); xSemaphoreGive(m);
    if (rc!=ESP_OK) ESP_LOGE("hp_firmware","HP P1006 firmware unavailable/failed; upload sihpP1006.dl at /hp-firmware");
    return rc;
}
