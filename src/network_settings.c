#include "network_settings.h"
#include "wifi.h"
#include "cloud_update.h"
#include "brand.h"
#include "usb_printer.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include "lwip/sockets.h"
#include <stdlib.h>
static esp_timer_handle_t restart_timer;
static void restart_idle(void *ctx) {
    if(usb_printer_job_active()||cloud_update_busy()||wifi_setup_pending()) { esp_timer_start_once(restart_timer,3000000); return; }
    esp_restart();
}
void network_settings_load(network_config_t *c) {
    memset(c,0,sizeof(*c)); nvs_handle_t h;
    if(nvs_open("print_net",NVS_READONLY,&h)==ESP_OK) {
        size_t n=sizeof(*c); if(nvs_get_blob(h,"v1",c,&n)!=ESP_OK || n!=sizeof(*c)) memset(c,0,sizeof(*c));
        nvs_close(h);
    }
}
esp_err_t network_settings_save(const network_config_t *c) {
    nvs_handle_t h; esp_err_t e=nvs_open("print_net",NVS_READWRITE,&h);
    if(e==ESP_OK) { e=nvs_set_blob(h,"v1",c,sizeof(*c)); if(e==ESP_OK)e=nvs_commit(h); nvs_close(h); } return e;
}
esp_err_t network_settings_reset(void) { network_config_t c={0}; return network_settings_save(&c); }
static const char page[]=
"<!doctype html><html lang=vi><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Cài đặt IP ESP 32</title>"
"<style>body{font:17px system-ui;max-width:620px;margin:30px auto;padding:20px;background:#f3f7fa}label{display:block;margin:14px 0}input,select,button{font:inherit;padding:10px;box-sizing:border-box;width:100%;border:1px solid #acbdc8;border-radius:8px}button{background:#087e81;color:white}a{color:#087e81}small{display:block}</style>"
"<nav><a href='/'>Trang in</a> · <a href='/setup'>Đổi Wi-Fi</a></nav>\n"
"<h1>Địa chỉ IP</h1>\n"
"<p id=current>Đang đọc địa chỉ IP…</p>\n"
"<form id=form>\n"
"<label>Chế độ IP<select id=mode disabled><option value=dhcp>IP động (DHCP)</option><option value=static>IP tĩnh — giữ IP đang dùng</option></select></label>\n"
"<p id=preview></p>\n"
"<button id=save disabled>Lưu chế độ IP</button>\n"
"</form>\n"
"<p id=status role=status></p>\n"
"<script>\n"
"const $=id=>document.getElementById(id);\n"
"let current='', loaded=false, saving=false;\n"
"function render(){\n"
"  const fixed=$('mode').value==='static';\n"
"  $('preview').textContent=fixed\n"
"    ? (current ? 'ESP sẽ giữ IP '+current+' làm IP tĩnh.' : 'ESP chưa nhận IP từ router.')\n"
"    : 'Router sẽ tự cấp IP cho ESP.';\n"
"  $('save').disabled=!loaded||saving||(fixed&&!current);\n"
"}\n"
"$('mode').onchange=render;\n"
"fetch('/api/network',{cache:'no-store'}).then(async r=>{\n"
"  if(!r.ok)throw Error('Không đọc được cấu hình IP.');\n"
"  const config=await r.json();\n"
"  current=config.current;\n"
"  $('current').textContent=current?'IP đang dùng: '+current:'ESP chưa có địa chỉ IP.';\n"
"  $('mode').value=config.mode;\n"
"  $('mode').disabled=false;\n"
"  loaded=true;\n"
"  render();\n"
"}).catch(e=>$('status').textContent=e.message);\n"
"$('form').onsubmit=async e=>{\n"
"  e.preventDefault();\n"
"  if($('save').disabled)return;\n"
"  saving=true;\n"
"  render();\n"
"  $('status').textContent='Đang lưu…';\n"
"  try{\n"
"    const r=await fetch('/api/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:$('mode').value})});\n"
"    const config=await r.json();\n"
"    if(!r.ok)throw Error(config.message);\n"
"    $('status').textContent=config.message;\n"
"    if(config.url){\n"
"      const link=document.createElement('a');\n"
"      link.href=config.url;\n"
"      link.textContent=' Mở trang in';\n"
"      $('status').append(link);\n"
"    }\n"
"  }catch(e){\n"
"    $('status').textContent=e.message;\n"
"    saving=false;\n"
"    render();\n"
"  }\n"
"};\n"
"</script></html>\n";
static void response(airprint_http_t *h,const char *status,const char *message,const char *url) {
    cJSON *j=cJSON_CreateObject(); if(!j){airprint_http_send(h,"500 Internal Server Error","text/plain","No memory",9);return;}
    cJSON_AddStringToObject(j,"message",message);if(url)cJSON_AddStringToObject(j,"url",url);
    char *s=cJSON_PrintUnformatted(j);if(s)airprint_http_send_ex(h,status,"application/json","Cache-Control: no-store\r\n",s,strlen(s));
    free(s);cJSON_Delete(j);
}
static const char *value(cJSON *j,const char *key) { cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)?v->valuestring:NULL; }
bool network_settings_http(airprint_http_t *h,const char *method,const char *path) {
    bool html=!strcmp(path,"/network");if(!html&&strcmp(path,"/api/network"))return false;
    if(html&&!strcmp(method,"GET")){airprint_http_send(h,"200 OK","text/html; charset=utf-8",page,sizeof(page)-1);return true;}
    if(!html&&!strcmp(method,"GET")) {
        network_config_t c;network_settings_load(&c);char ip[24]="",mask[24]="255.255.255.0",gw[24]="",dns[24]="1.1.1.1",current[24];
        wifi_get_ip(current,sizeof(current));
        if(c.enabled){inet_ntop(AF_INET,&c.ip,ip,sizeof(ip));inet_ntop(AF_INET,&c.mask,mask,sizeof(mask));inet_ntop(AF_INET,&c.gateway,gw,sizeof(gw));inet_ntop(AF_INET,&c.dns,dns,sizeof(dns));}
        else { snprintf(ip,sizeof(ip),"%s",current);wifi_get_network(mask,sizeof(mask),gw,sizeof(gw)); }
        char body[320];int n=snprintf(body,sizeof(body),"{\"mode\":\"%s\",\"ip\":\"%s\",\"mask\":\"%s\",\"gateway\":\"%s\",\"dns\":\"%s\",\"current\":\"%s\"}",c.enabled?"static":"dhcp",ip,mask,gw,dns,current);
        airprint_http_send_ex(h,"200 OK","application/json","Cache-Control: no-store\r\n",body,n);return true;
    }
    if(html||strcmp(method,"POST")){response(h,"405 Method Not Allowed","Chỉ hỗ trợ GET/POST.",NULL);return true;}
    if(usb_printer_job_active()||cloud_update_busy()||wifi_setup_pending()){response(h,"409 Conflict","Đang in. Hãy lưu cài đặt sau khi in xong.",NULL);return true;}
    char body[513];int n=airprint_http_read_body(h,body,512);if(n<=0||n>=512){response(h,"400 Bad Request","Dữ liệu quá dài hoặc trống.",NULL);return true;}body[n]=0;
    cJSON *j=cJSON_ParseWithLength(body,n);const char *mode=value(j,"mode");network_config_t c={0};
    bool fixed=mode&&!strcmp(mode,"static");
    bool valid=fixed||(mode&&!strcmp(mode,"dhcp"));
    cJSON_Delete(j);
    if(!valid){response(h,"400 Bad Request","Hãy chọn IP động hoặc IP tĩnh.",NULL);return true;}
    if(fixed&&!wifi_current_ipv4_config(&c)){
        response(h,"409 Conflict","ESP chưa có cấu hình IP hợp lệ từ Wi-Fi. Kết nối lại rồi thử lưu IP tĩnh.",NULL);
        return true;
    }
    if(!restart_timer){const esp_timer_create_args_t a={.callback=restart_idle,.name="net_restart"};if(esp_timer_create(&a,&restart_timer)!=ESP_OK){response(h,"500 Internal Server Error","Không tạo được lịch khởi động lại.",NULL);return true;}}
    if(network_settings_save(&c)!=ESP_OK){response(h,"500 Internal Server Error","Không lưu được cấu hình.",NULL);return true;}
    char address[24],url[64];inet_ntop(AF_INET,&c.ip,address,sizeof(address));snprintf(url,sizeof(url),"http://%s/",address);
    response(h,"200 OK",c.enabled?"Đã lưu địa chỉ đang dùng làm IP tĩnh. ESP sẽ khởi động lại sau vài giây.":"Đã lưu DHCP. Sau khi khởi động lại, xem IP trên router hoặc kết nối USB để biết địa chỉ mới.",c.enabled?url:NULL);
    esp_timer_stop(restart_timer);esp_timer_start_once(restart_timer,3000000);return true;
}
