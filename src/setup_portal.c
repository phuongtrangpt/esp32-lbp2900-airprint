#include "brand.h"
#include "cJSON.h"
#include "cloud_update.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "network_settings.h"
#include "portal_dns.h"
#include "usb_printer.h"
#include "wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const char page[] =
    "<!doctype html><html lang=vi><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>vietphuong esp · Thiết lập</title><style>*{box-sizing:border-box}html{color-scheme:dark}body{font:15px/1.6 system-ui;max-width:760px;margin:0 auto;padding:32px 18px 24px;background:#080f1d;color:#ecf3ff}body:before{content:\"\";position:fixed;inset:0;pointer-events:none;background:radial-gradient(ellipse at 90% 0, #16395166,transparent 60%);z-index:-1}header{padding:20px 4px 26px}h1{font-size:34px;line-height:1.2;letter-spacing:-1px;margin:12px 0}h2{font-size:19px;margin:0 0 8px}p{color:#a9bad1;margin:8px 0 18px}.eyebrow{color:#65e3cc;font-size:12px;font-weight:750;letter-spacing:2px}.card{padding:24px;margin:0 0 18px;background:linear-gradient(145deg,#17253a,#101c2d);border:1px solid #2c405a;border-radius:22px;box-shadow:0 16px 35px #0003}.step{display:inline-grid;place-items:center;width:29px;height:29px;border-radius:10px;background:#63e6cb22;color:#70efdb;font-size:13px;margin-right:8px}input,select,button{box-sizing:border-box;width:100%;min-height:48px;padding:12px 14px;border-radius:12px;border:1px solid #354c68;background:#0c1728;color:#eff6ff;font:inherit}input:focus,select:focus{outline:2px solid #63e6cb;border-color:transparent}label{display:block;margin:16px 0 0;color:#c5d4e7}label input,label select{margin-top:7px}button{background:#6ce6cc;color:#071c22;border-color:transparent;font-weight:750;cursor:pointer;margin:16px 0 8px}button:hover{filter:brightness(1.08)}button:disabled{opacity:.55;cursor:wait}#scan{background:#20364d;color:#8ff2e0;border:1px solid #395c72}a{color:#82ebd7;text-underline-offset:4px}#status:empty,#result:empty{display:none}#status{padding:16px;border:1px solid #345870;background:#112b3d;border-radius:14px;color:#d5e8ff}#result{padding:24px;background:#11372e;border:1px solid #53c9a5;border-radius:18px;margin:18px 0}#result a{display:block;font-size:25px;font-weight:800;overflow-wrap:anywhere}#result p{color:#d9f3e9;margin:12px 0 0}footer{display:flex;flex-wrap:wrap;justify-content:space-between;gap:12px;padding:20px 4px;color:#8fa3be;font-size:12px}details summary{cursor:pointer;color:#cad8eb}.hint{font-size:13px}[hidden]{display:none!important}@media(max-width:480px){body{padding:14px 12px}.card{padding:18px}h1{font-size:28px}#result a{font-size:21px}}</style><header><span class=eyebrow>VIETPHUONG ESP / KẾT NỐI KHÔNG DÂY</span><h1>Máy in cũ. Trải nghiệm mới.</h1><p>Kết nối Wi-Fi lần đầu hoặc đổi tên mạng và mật khẩu.</p><p><a href='https://www.facebook.com/leviethoaiphuong' target=_blank rel=noopener>Facebook · Hỗ trợ Việt Phương</a> · <a href='/'>Trang in</a></p></header><form id=form><section class=card><h2><span class=step>01</span>Kết nối Wi-Fi nhà bạn</h2><p class=hint>Chỉ tìm Wi-Fi 2.4 GHz · Mạng mạnh nhất ở đầu danh sách.</p><p id=info>Đang đọc trạng thái Wi-Fi…</p><p class=hint>Nếu Wi-Fi cũ đã đổi tên hoặc mật khẩu, giữ BOOT 5 giây, kết nối vào Wi-Fi <strong>vietphuong esp-XXXX</strong> của ESP, rồi mở <strong>http://192.168.4.1/setup</strong>.</p><button id=scan type=button disabled>Tìm mạng Wi-Fi 2.4 GHz</button><select id=networks aria-label='Mạng tìm thấy'><option>Chọn mạng hoặc nhập tên bên dưới</option></select><label>Tên Wi-Fi<input id=ssid required maxlength=32 autocomplete=off></label><label>Mật khẩu Wi-Fi<input id=password type=password maxlength=63 autocomplete=new-password></label></section><button id=save disabled>Kết nối Wi-Fi</button></form><p id=status role=status></p><div id=result></div><p>ESP tự nhận IP từ router khi kết nối Wi-Fi. Sau khi kết nối, anh có thể mở mục <strong>Địa chỉ IP</strong> trên trang in để giữ địa chỉ đó làm IP tĩnh.</p><details class=card><summary>Bàn giao thiết bị cho khách</summary><p>Xóa Wi-Fi và IP đã thử, giữ firmware HP. Giữ BOOT 5 giây rồi bấm nút bên dưới.</p><button id=handover type=button>Bàn giao · xóa Wi-Fi đã lưu</button></details><footer><span>© Việt Phương · vietphuong esp</span><span id=firmware>Đang đọc phiên bản…</span><a href='https://www.facebook.com/leviethoaiphuong' target=_blank rel=noopener>Facebook hỗ trợ ↗</a></footer>\n"
    "<script>\n"
    "const $=id=>document.getElementById(id);\n"
    "let waiting=false, submitting=false, scanning=false, ap=false, scanned=false, connected=false, busy=false;\n"
    "function buttons(){\n"
    "  $('scan').disabled=(!ap&&!connected)||busy||waiting||submitting||scanning;\n"
    "  $('save').disabled=(!ap&&!connected)||busy||waiting||submitting||scanning;\n"
    "}\n"
    "$('networks').onchange=()=>{$('ssid').value=$('networks').value};\n"
    "$('scan').onclick=async()=>{\n"
    "  if((!ap&&!connected)||busy||scanning||submitting||waiting)return;\n"
    "  scanning=true;\n"
    "  buttons();\n"
    "  $('status').textContent='Đang tìm Wi-Fi 2.4 GHz…';\n"
    "  try{\n"
    "    const r=await fetch('/api/setup/scan',{cache:'no-store'});\n"
    "    const j=await r.json();\n"
    "    if(!r.ok)throw Error(j.message);\n"
    "    const seen=new Set();\n"
    "    const networks=j.networks.filter(n=>n.ssid&&(!n.channel||n.channel<=14))\n"
    "      .sort((a,b)=>b.rssi-a.rssi).filter(n=>{\n"
    "        if(seen.has(n.ssid))return false;\n"
    "        seen.add(n.ssid);\n"
    "        return true;\n"
    "      });\n"
    "    $('networks').replaceChildren();\n"
    "    const placeholder=document.createElement('option');\n"
    "    placeholder.value='';\n"
    "    placeholder.textContent=networks.length?'Chọn Wi-Fi nhà bạn ('+networks.length+' mạng)':'Không thấy mạng · Nhập tên bên dưới';\n"
    "    $('networks').append(placeholder);\n"
    "    for(const n of networks){\n"
    "      const option=document.createElement('option');\n"
    "      option.value=n.ssid;\n"
    "      option.textContent=n.ssid+' ('+n.rssi+' dBm)'+(n.rssi>=-60?' · Sóng mạnh':'');\n"
    "      $('networks').append(option);\n"
    "    }\n"
    "    $('status').textContent=networks.length?'':'Không thấy mạng 2.4 GHz. Có thể nhập tên mạng thủ công.';\n"
    "  }catch(e){$('status').textContent=e.message}\n"
    "  finally{\n"
    "    scanning=false;\n"
    "    $('scan').textContent='Quét lại Wi-Fi 2.4 GHz';\n"
    "    buttons();\n"
    "  }\n"
    "};\n"
    "$('form').onsubmit=async e=>{\n"
    "  e.preventDefault();\n"
    "  if($('save').disabled)return;\n"
    "  submitting=true;\n"
    "  buttons();\n"
    "  $('result').replaceChildren();\n"
    "  try{\n"
    "    const data={ssid:$('ssid').value,password:$('password').value,mode:'dhcp'};\n"
    "    const r=await fetch('/api/setup',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});\n"
    "    const j=await r.json();\n"
    "    if(!r.ok)throw Error(j.message);\n"
    "    waiting=true;\n"
    "    $('status').textContent=j.message+' Nếu trang mất kết nối, chuyển sang Wi-Fi mới và mở lại trang in. Nếu thất bại, ESP sẽ quay về Wi-Fi cũ.';\n"
    "  }catch(e){\n"
    "    $('status').textContent=e.message;\n"
    "  }finally{\n"
    "    submitting=false;\n"
    "    buttons();\n"
    "  }\n"
    "};\n"
    "async function poll(){\n"
    "  try{\n"
    "    const r=await fetch('/api/setup/status',{cache:'no-store'});\n"
    "    const j=await r.json();\n"
    "    $('firmware').textContent='Firmware '+(j.firmware||'—');\n"
    "    ap=j.ap;\n"
    "    connected=j.connected;\n"
    "    busy=!!(j.pending||j.busy);\n"
    "    buttons();\n"
    "    $('info').textContent=ap?'Wi-Fi thiết lập: '+j.ap_name:connected?'Đang dùng Wi-Fi hiện tại. Chọn mạng mới bên dưới để đổi Wi-Fi.':'Chưa có kết nối. Giữ BOOT 5 giây để mở Wi-Fi thiết lập.';\n"
    "    if((ap||connected)&&!busy&&!scanned&&!waiting&&!submitting){\n"
    "      scanned=true;\n"
    "      $('scan').onclick();\n"
    "    }else if(!ap&&!connected)scanned=false;\n"
    "    if(!waiting)return;\n"
    "    if(j.pending)return;\n"
    "    if(j.error){\n"
    "      waiting=false;\n"
    "      $('status').textContent='Chưa kết nối/lưu được. Kiểm tra tên Wi-Fi hoặc mật khẩu rồi thử lại.';\n"
    "    }else if(j.connected&&j.ip){\n"
    "      waiting=false;\n"
    "      $('password').value='';\n"
    "      $('status').textContent='Đã kết nối và lưu Wi-Fi. Chuyển điện thoại về Wi-Fi nhà để mở trang in hoặc giữ IP này làm IP tĩnh.';\n"
    "      const base='http://'+j.ip;\n"
    "      const print=document.createElement('a');\n"
    "      print.href=base+'/';\n"
    "      print.textContent=print.href;\n"
    "      const fixed=document.createElement('a');\n"
    "      fixed.href=base+'/network';\n"
    "      fixed.textContent='Chọn IP tĩnh cho '+j.ip;\n"
    "      $('result').replaceChildren(print,fixed);\n"
    "    }\n"
    "    buttons();\n"
    "  }catch(e){\n"
    "    if(waiting)$('status').textContent='Đang đổi Wi-Fi. Chuyển sang mạng mới rồi mở lại trang in; nếu thất bại, ESP sẽ quay về Wi-Fi cũ. Có thể kết nối hotspot vietphuong esp để kiểm tra.';\n"
    "  }\n"
    "}\n"
    "setInterval(poll,2000);\n"
    "poll();\n"
    "$('handover').onclick=async()=>{\n"
    "  if(!confirm('Xóa Wi-Fi/IP đã lưu để bàn giao cho khách?'))return;\n"
    "  try{\n"
    "    const r=await fetch('/api/setup/handover',{method:'POST'});\n"
    "    $('status').textContent=(await r.json()).message;\n"
    "  }catch(e){$('status').textContent=e.message}\n"
    "};\n"
    "</script></html>\n";
static void json(airprint_http_t *h, const char *status, cJSON *j) {
  char *s = cJSON_PrintUnformatted(j);
  if (s) {
    airprint_http_send_ex(h, status, "application/json",
                          "Cache-Control: no-store\r\n", s, strlen(s));
    free(s);
  } else
    airprint_http_send(h, "500 Internal Server Error", "text/plain", "Memory",
                       6);
  cJSON_Delete(j);
}
static void message(airprint_http_t *h, const char *status, const char *text) {
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "message", text);
  json(h, status, j);
}
static const char *value(cJSON *j, const char *k) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(j, k);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}
bool wifi_setup_http(airprint_http_t *h, const char *method, const char *path) {
  char route[128];
  size_t len = strcspn(path, "?");
  if (len >= sizeof(route))
    return false;
  memcpy(route, path, len);
  route[len] = 0;
  path = route;
  bool get = !strcmp(method, "GET");
  if (get && wifi_setup_active() &&
      (!strcmp(path, "/generate_204") || !strcmp(path, "/gen_204") ||
       !strcmp(path, "/hotspot-detect.html") ||
       !strcmp(path, "/connecttest.txt") || !strcmp(path, "/ncsi.txt") ||
       (!strcmp(path, "/") &&
        !strncmp(airprint_http_host(h), VP_SETUP_IP, strlen(VP_SETUP_IP))))) {
    airprint_http_send_ex(h, "302 Found", "text/plain",
                          "Location: http://" VP_SETUP_IP
                          "/setup\r\nCache-Control: no-store\r\n",
                          "Setup", 5);
    return true;
  }
  if (get && !strcmp(path, "/setup")) {
    airprint_http_send(h, "200 OK", "text/html; charset=utf-8", page,
                       sizeof(page) - 1);
    return true;
  }
  if (get && !strcmp(path, "/api/setup/status")) {
    cJSON *j = cJSON_CreateObject();
    char ip[24];
    wifi_get_ip(ip, sizeof(ip));
    cJSON_AddBoolToObject(j, "connected", wifi_connected());
    cJSON_AddBoolToObject(j, "pending", wifi_setup_pending());
    cJSON_AddBoolToObject(j, "ap", wifi_setup_active());
    cJSON_AddBoolToObject(j, "busy", usb_printer_job_active() || cloud_update_busy());
    cJSON_AddNumberToObject(j, "error", wifi_setup_error());
    cJSON_AddStringToObject(j, "ip", ip);
    cJSON_AddStringToObject(j, "ap_name", wifi_setup_name());
    cJSON_AddStringToObject(j, "firmware", esp_app_get_description()->version);
    json(h, "200 OK", j);
    return true;
  }
  if (get && !strcmp(path, "/api/setup/scan")) {
    if ((!wifi_setup_active() && !wifi_connected()) || wifi_setup_pending() ||
        usb_printer_job_active() || cloud_update_busy()) {
      message(h, "409 Conflict",
              "Chỉ quét Wi-Fi khi ESP có kết nối hoặc đang mở thiết lập và máy đang rảnh.");
      return true;
    }
    uint16_t n = 24;
    wifi_ap_record_t *aps = calloc(n, sizeof(*aps));
    if (!aps) {
      message(h, "503 Service Unavailable", "Không đủ RAM để quét Wi-Fi.");
      return true;
    }
    if (wifi_scan_networks(aps, &n) != ESP_OK) {
      free(aps);
      message(h, "503 Service Unavailable",
              "Chưa quét được Wi-Fi. Chờ một lát rồi quét lại, hoặc nhập tên mạng.");
      return true;
    }
    cJSON *j = cJSON_CreateObject(),
          *arr = cJSON_AddArrayToObject(j, "networks");
    {
      /* Sort explicitly, then collapse roaming APs sharing an SSID. */
      for (int i = 1; i < n; i++) {
        wifi_ap_record_t ap = aps[i];
        int k = i;
        while (k > 0 && aps[k - 1].rssi < ap.rssi) { aps[k] = aps[k - 1]; k--; }
        aps[k] = ap;
      }
      for (int i = 0; i < n; i++) {
        if (!aps[i].ssid[0] || aps[i].primary < 1 || aps[i].primary > 14)
          continue;
        bool duplicate = false;
        for (int k = 0; k < i; k++)
          if (aps[k].primary >= 1 && aps[k].primary <= 14 && !strcmp((const char *)aps[k].ssid, (const char *)aps[i].ssid)) { duplicate = true; break; }
        if (duplicate) continue;
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "ssid", (const char *)aps[i].ssid);
        cJSON_AddNumberToObject(a, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(a, "channel", aps[i].primary);
        cJSON_AddItemToArray(arr, a);
      }
    }
    free(aps);
    json(h, "200 OK", j);
    return true;
  }
  if (!strcmp(path, "/api/setup/handover") && !strcmp(method, "POST")) {
    if (!cloud_update_allowed()) {
      message(h, "403 Forbidden", "Giữ BOOT 5 giây trên mạch rồi bấm lại.");
      return true;
    }
    if (usb_printer_job_active() || cloud_update_busy() ||
        wifi_setup_pending()) {
      message(h, "409 Conflict", "Máy đang bận.");
      return true;
    }
    bool ok = wifi_handover() == ESP_OK;
    message(h, ok ? "202 Accepted" : "500 Internal Server Error",
            ok ? "Đã xóa Wi-Fi/IP. ESP sẽ khởi động lại khi rảnh; giữ nguyên "
                 "firmware HP."
               : "Không lưu được cấu hình.");
    return true;
  }
  if (strcmp(path, "/api/setup"))
    return false;
  if (strcmp(method, "POST")) {
    message(h, "405 Method Not Allowed", "Chỉ hỗ trợ POST.");
    return true;
  }
  char b[769];
  int n = airprint_http_read_body(h, b, 768);
  if (n <= 0 || n >= 768) {
    message(h, "400 Bad Request", "Dữ liệu quá dài hoặc trống.");
    return true;
  }
  b[n] = 0;
  cJSON *j = cJSON_ParseWithLength(b, n);
  const char *mode = value(j, "mode"), *ssid = value(j, "ssid"),
             *password = value(j, "password");
  network_config_t cfg = {0};
  bool valid =
      mode && (!strcmp(mode, "dhcp") ||
               (!strcmp(mode, "static") &&
                network_config_parse(&cfg, value(j, "ip"), value(j, "mask"),
                                     value(j, "gateway"), value(j, "dns"))));
  esp_err_t e = valid ? wifi_submit_setup(ssid, password, &cfg, false)
                      : ESP_ERR_INVALID_ARG;
  cJSON_Delete(j);
  memset(b, 0, sizeof(b));
  message(h,
          e == ESP_OK                ? "202 Accepted"
          : e == ESP_ERR_INVALID_ARG ? "400 Bad Request"
                                     : "409 Conflict",
          e == ESP_OK ? "Đang kết nối, vui lòng chờ tối đa 30 giây…"
          : e == ESP_ERR_INVALID_ARG
              ? "Tên Wi-Fi/mật khẩu/IP không hợp lệ. Mật khẩu 8–63 ký tự hoặc "
                "để trống cho mạng mở."
              : "Máy đang bận hoặc mất kết nối. Chờ máy rảnh rồi thử "
                "lại.");
  return true;
}
static void dns_task(void *arg) {
  int fd = -1;
  uint8_t q[512], out[528];
  const uint8_t ip[] = {192, 168, 4, 1};
  for (;;) {
    if (!wifi_setup_active()) {
      if (fd >= 0) {
        close(fd);
        fd = -1;
      }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (fd < 0) {
      fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (fd < 0) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
      struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(53)};
      inet_pton(AF_INET, VP_SETUP_IP, &a.sin_addr);
      struct timeval t = {.tv_sec = 1};
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
      if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(fd);
        fd = -1;
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
    }
    struct sockaddr_in peer;
    socklen_t l = sizeof(peer);
    int n = recvfrom(fd, q, sizeof(q), 0, (struct sockaddr *)&peer, &l);
    if (n > 0) {
      size_t size = portal_dns_reply(q, n, out, sizeof(out), ip);
      if (size)
        sendto(fd, out, size, 0, (struct sockaddr *)&peer, l);
    }
  }
}
void setup_dns_start(void) {
  if (xTaskCreate(dns_task, "portal_dns", 3072, NULL, 2, NULL) != pdPASS)
    ESP_LOGE("setup", "DNS task allocation failed");
}
