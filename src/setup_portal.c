#include "wifi.h"
#include "brand.h"
#include "cloud_update.h"
#include "network_settings.h"
#include "usb_printer.h"
#include "portal_dns.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
static const char page[]=
"<!doctype html><html lang=vi><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>vietphuong esp · Thiết lập</title>"
"<style>body{font:16px system-ui;max-width:620px;margin:24px auto;padding:20px;background:#f1f6f7;color:#173239}input,select,button{box-sizing:border-box;width:100%;padding:12px;border-radius:9px;border:1px solid #9bb2bc;font:inherit}label{display:block;margin:15px 0}button{background:#087e81;color:white;margin:8px 0}a{color:#087e81}progress{width:100%}</style>"
"<h1>vietphuong esp</h1><p>Cài Wi-Fi nhà và địa chỉ IP cho máy in.</p><p><a href='" VP_FACEBOOK "' target=_blank rel=noopener>Facebook · Hỗ trợ Việt Phương</a> · <a href='/'>Trang in</a></p>"
"<p id=info></p><button id=scan type=button>Tìm mạng Wi-Fi 2.4 GHz</button><select id=networks aria-label='Mạng tìm thấy'><option>Chọn mạng hoặc nhập tên bên dưới</option></select>"
"<form id=form><label>Tên Wi-Fi<input id=ssid required maxlength=32 autocomplete=off></label><label>Mật khẩu Wi-Fi<input id=password type=password maxlength=63 autocomplete=new-password></label>"
"<label>Địa chỉ IP<select id=mode><option value=dhcp>Tự động (DHCP)</option><option value=static>IP tĩnh</option></select></label>"
"<div id=fields hidden><label>IP<input id=ip placeholder=192.168.1.240></label><label>Subnet mask<input id=mask value=255.255.255.0></label><label>Gateway<input id=gateway placeholder=192.168.1.1></label><label>DNS<input id=dns value=1.1.1.1></label></div>"
"<button id=save>Kết nối và lưu</button></form><p id=status role=status></p><div id=result></div>"
"<p>Chọn IP chưa bị thiết bị khác dùng. Không biết thông số mạng thì chọn DHCP; có thể đổi IP sau tại /network.</p>"
"<details><summary>Bàn giao thiết bị cho khách</summary><p>Xóa Wi-Fi và IP đã thử, giữ firmware HP. Giữ BOOT 5 giây rồi bấm nút bên dưới.</p><button id=handover type=button>Bàn giao · xóa Wi-Fi đã lưu</button></details>"
"<script>const $=id=>document.getElementById(id),keys=['mode','ip','mask','gateway','dns'];let waiting=false;"
"$('mode').onchange=()=>{const fixed=$('mode').value==='static';$('fields').hidden=!fixed;for(const k of keys.slice(1)){$(k).required=fixed;$(k).disabled=!fixed}};$('mode').onchange();"
"$('networks').onchange=()=>{$('ssid').value=$('networks').value};"
"$('scan').onclick=async()=>{$('scan').disabled=true;try{const r=await fetch('/api/setup/scan',{cache:'no-store'}),j=await r.json();if(!r.ok)throw Error(j.message);$('networks').replaceChildren();for(const n of j.networks){const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)';$('networks').append(o)}if(j.networks.length)$('ssid').value=j.networks[0].ssid;else $('status').textContent='Không tìm thấy mạng. Có thể nhập tên Wi-Fi thủ công.'}catch(e){$('status').textContent=e.message}finally{$('scan').disabled=false}};"
"$('form').onsubmit=async e=>{e.preventDefault();$('save').disabled=true;try{const data=Object.fromEntries(['ssid','password',...keys].map(k=>[k,$(k).value]));const r=await fetch('/api/setup',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}),j=await r.json();if(!r.ok)throw Error(j.message);waiting=true;$('status').textContent=j.message}catch(e){$('status').textContent=e.message;$('save').disabled=false}};"
"async function poll(){try{const r=await fetch('/api/setup/status',{cache:'no-store'}),j=await r.json();$('info').textContent='Wi-Fi thiết lập: '+j.ap_name;if(waiting){if(j.error){waiting=false;$('save').disabled=false;$('status').textContent='Chưa kết nối/lưu được. Kiểm tra mật khẩu và IP rồi thử lại.'}else if(j.connected&&!j.pending){waiting=false;$('password').value='';$('status').textContent='Đã kết nối và lưu. Chuyển điện thoại về Wi-Fi nhà, rồi mở địa chỉ bên dưới. Wi-Fi thiết lập sẽ tắt sau ít giây.';const a=document.createElement('a');a.href='http://'+j.ip+'/';a.textContent=a.href;$('result').replaceChildren(a)}}}catch(e){if(waiting)$('status').textContent='Đang chờ ESP. Nếu Wi-Fi thiết lập đã tắt, kết nối lại Wi-Fi nhà.'}}setInterval(poll,2000);poll();"
"$('handover').onclick=async()=>{if(!confirm('Xóa Wi-Fi/IP đã lưu để bàn giao cho khách?'))return;try{const r=await fetch('/api/setup/handover',{method:'POST'}),j=await r.json();$('status').textContent=j.message}catch(e){$('status').textContent=e.message}};</script></html>";
static void json(airprint_http_t *h,const char *status,cJSON *j){char *s=cJSON_PrintUnformatted(j);if(s){airprint_http_send_ex(h,status,"application/json","Cache-Control: no-store\r\n",s,strlen(s));free(s);}else airprint_http_send(h,"500 Internal Server Error","text/plain","Memory",6);cJSON_Delete(j);}
static void message(airprint_http_t *h,const char *status,const char *text){cJSON *j=cJSON_CreateObject();cJSON_AddStringToObject(j,"message",text);json(h,status,j);}
static const char *value(cJSON *j,const char *k){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(v)?v->valuestring:NULL;}
bool wifi_setup_http(airprint_http_t *h,const char *method,const char *path){
 char route[128];size_t len=strcspn(path,"?");if(len>=sizeof(route))return false;memcpy(route,path,len);route[len]=0;path=route;
 bool get=!strcmp(method,"GET");
 if(get&&wifi_setup_active()&&(!strcmp(path,"/generate_204")||!strcmp(path,"/gen_204")||!strcmp(path,"/hotspot-detect.html")||!strcmp(path,"/connecttest.txt")||!strcmp(path,"/ncsi.txt")||(!strcmp(path,"/")&&!strncmp(airprint_http_host(h),VP_SETUP_IP,strlen(VP_SETUP_IP))))){
  airprint_http_send_ex(h,"302 Found","text/plain","Location: http://" VP_SETUP_IP "/setup\r\nCache-Control: no-store\r\n","Setup",5);return true;
 }
 if(get&&!strcmp(path,"/setup")){airprint_http_send(h,"200 OK","text/html; charset=utf-8",page,sizeof(page)-1);return true;}
 if(get&&!strcmp(path,"/api/setup/status")){
  cJSON *j=cJSON_CreateObject();char ip[24];wifi_get_ip(ip,sizeof(ip));
  cJSON_AddBoolToObject(j,"connected",wifi_connected());cJSON_AddBoolToObject(j,"pending",wifi_setup_pending());cJSON_AddBoolToObject(j,"ap",wifi_setup_active());cJSON_AddNumberToObject(j,"error",wifi_setup_error());cJSON_AddStringToObject(j,"ip",ip);cJSON_AddStringToObject(j,"ap_name",wifi_setup_name());json(h,"200 OK",j);return true;
 }
 if(get&&!strcmp(path,"/api/setup/scan")){
  if(!wifi_setup_active()||wifi_setup_pending()||usb_printer_job_active()||cloud_update_busy()){message(h,"409 Conflict","Giữ BOOT 5 giây để mở thiết lập; quét mạng khi máy đang rảnh.");return true;}
  wifi_scan_config_t cfg={0};if(esp_wifi_scan_start(&cfg,true)!=ESP_OK){message(h,"503 Service Unavailable","Wi-Fi đang kết nối. Chờ một lát rồi quét lại, hoặc nhập tên mạng.");return true;}
  uint16_t n=24;wifi_ap_record_t *aps=calloc(n,sizeof(*aps));if(!aps){esp_wifi_clear_ap_list();message(h,"503 Service Unavailable","Không đủ RAM.");return true;}
  cJSON *j=cJSON_CreateObject(),*arr=cJSON_AddArrayToObject(j,"networks");if(esp_wifi_scan_get_ap_records(&n,aps)==ESP_OK)for(int i=0;i<n;i++){if(!aps[i].ssid[0])continue;cJSON *a=cJSON_CreateObject();cJSON_AddStringToObject(a,"ssid",(const char *)aps[i].ssid);cJSON_AddNumberToObject(a,"rssi",aps[i].rssi);cJSON_AddItemToArray(arr,a);}free(aps);json(h,"200 OK",j);return true;
 }
 if(!strcmp(path,"/api/setup/handover")&&!strcmp(method,"POST")){
  if(!cloud_update_allowed()){message(h,"403 Forbidden","Giữ BOOT 5 giây trên mạch rồi bấm lại.");return true;}
  if(usb_printer_job_active()||cloud_update_busy()||wifi_setup_pending()){message(h,"409 Conflict","Máy đang bận.");return true;}
  bool ok=wifi_handover()==ESP_OK;message(h,ok?"202 Accepted":"500 Internal Server Error",ok?"Đã xóa Wi-Fi/IP. ESP sẽ khởi động lại khi rảnh; giữ nguyên firmware HP.":"Không lưu được cấu hình.");return true;
 }
 if(strcmp(path,"/api/setup"))return false;
 if(strcmp(method,"POST")){message(h,"405 Method Not Allowed","Chỉ hỗ trợ POST.");return true;}
 char b[769];int n=airprint_http_read_body(h,b,768);if(n<=0||n>=768){message(h,"400 Bad Request","Dữ liệu quá dài hoặc trống.");return true;}b[n]=0;
 cJSON *j=cJSON_ParseWithLength(b,n);const char *mode=value(j,"mode"),*ssid=value(j,"ssid"),*password=value(j,"password");network_config_t cfg={0};
 bool valid=mode&&(!strcmp(mode,"dhcp")||(!strcmp(mode,"static")&&network_config_parse(&cfg,value(j,"ip"),value(j,"mask"),value(j,"gateway"),value(j,"dns"))));
 esp_err_t e=valid?wifi_submit_setup(ssid,password,&cfg,false):ESP_ERR_INVALID_ARG;cJSON_Delete(j);memset(b,0,sizeof(b));
 message(h,e==ESP_OK?"202 Accepted":e==ESP_ERR_INVALID_ARG?"400 Bad Request":"409 Conflict",e==ESP_OK?"Đang kết nối, vui lòng chờ tối đa 30 giây…":e==ESP_ERR_INVALID_ARG?"Tên Wi-Fi/mật khẩu/IP không hợp lệ. Mật khẩu 8–63 ký tự hoặc để trống cho mạng mở.":"Máy đang bận hoặc chưa mở thiết lập. Giữ BOOT 5 giây rồi thử lại.");return true;
}
static void dns_task(void *arg){
 int fd=-1;uint8_t q[512],out[528];const uint8_t ip[]={192,168,4,1};
 for(;;){
  if(!wifi_setup_active()){if(fd>=0){close(fd);fd=-1;}vTaskDelay(pdMS_TO_TICKS(500));continue;}
  if(fd<0){fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(fd<0){vTaskDelay(pdMS_TO_TICKS(1000));continue;}struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(53)};inet_pton(AF_INET,VP_SETUP_IP,&a.sin_addr);struct timeval t={.tv_sec=1};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));if(bind(fd,(struct sockaddr *)&a,sizeof(a))<0){close(fd);fd=-1;vTaskDelay(pdMS_TO_TICKS(1000));continue;}}
  struct sockaddr_in peer;socklen_t l=sizeof(peer);int n=recvfrom(fd,q,sizeof(q),0,(struct sockaddr *)&peer,&l);if(n>0){size_t size=portal_dns_reply(q,n,out,sizeof(out),ip);if(size)sendto(fd,out,size,0,(struct sockaddr *)&peer,l);}
 }
}
void setup_dns_start(void){if(xTaskCreate(dns_task,"portal_dns",3072,NULL,2,NULL)!=pdPASS)ESP_LOGE("setup","DNS task allocation failed");}
