#include "cloud_update.h"
#include "brand.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "usb_printer.h"
#include "wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool busy;
static int progress;
static int64_t allowed_until;
static char state[24] = "idle",
            message[320] = "Bấm kiểm tra để tìm phiên bản mới.",
            latest[32] = "", notes[640] = "";
static void status(const char *s, const char *m, int pct) {
  taskENTER_CRITICAL(&lock);
  snprintf(state, sizeof(state), "%s", s);
  snprintf(message, sizeof(message), "%s", m);
  progress = pct;
  taskEXIT_CRITICAL(&lock);
  ESP_LOGI("cloud_update", "%s: %s", s, m);
}
bool cloud_update_busy(void) {
  taskENTER_CRITICAL(&lock);
  bool b = busy;
  taskEXIT_CRITICAL(&lock);
  return b;
}
bool cloud_update_allowed(void) {
  taskENTER_CRITICAL(&lock);
  bool b = esp_timer_get_time() < allowed_until;
  taskEXIT_CRITICAL(&lock);
  return b;
}
void cloud_update_arm(void) {
  taskENTER_CRITICAL(&lock);
  allowed_until = esp_timer_get_time() + 120000000;
  taskEXIT_CRITICAL(&lock);
  wifi_request_setup();
  ESP_LOGI("cloud_update",
           "Physical BOOT confirmation: maintenance allowed for 120 seconds");
}
static const char page[] =
    "<!doctype html><html lang=vi><meta charset=utf-8><meta name=viewport "
    "content='width=device-width,initial-scale=1'><title>vietphuong esp · Cập "
    "nhật</title>"
    "<style>body{font:17px system-ui;max-width:650px;margin:30px "
    "auto;padding:20px;background:#f1f6f7;color:#173239}button,a{font:inherit;"
    "padding:12px;display:inline-block;margin:6px;border:0;border-radius:8px}"
    "button{background:#087e81;color:white}progress{width:100%;height:24px}</"
    "style>"
    "<h1>vietphuong esp</h1><p><a href='/'>Trang in</a> · <a href='" VP_FACEBOOK
    "' target=_blank rel=noopener>Facebook hỗ trợ</a></p>"
    "<h2>Cập nhật firmware qua Wi-Fi</h2><p id=version></p><p id=latest></p><p "
    "id=notes></p><button id=check>Kiểm tra phiên bản mới</button><button "
    "id=install disabled>Cập nhật</button>"
    "<progress id=progress max=100 value=0></progress><p id=status "
    "role=status></p><p>Trước khi cập nhật: giữ BOOT 5 giây trên mạch để xác "
    "nhận (hiệu lực 2 phút), sau đó bấm Cập nhật. Không rút điện trong lúc "
    "ghi. Wi-Fi/IP và file HP được giữ nguyên.</p>"
    "<p>Thiết bị tải trực tiếp bản đúng model từ máy chủ HTTPS. Khi gặp lỗi "
    "tải/kiểm tra, firmware đang dùng vẫn được giữ.</p>"
    "<script>const $=id=>document.getElementById(id);let sending=false;async "
    "function poll(){try{const r=await "
    "fetch('/api/update',{cache:'no-store'}),s=await "
    "r.json();$('version').textContent='Đang dùng: '+s.current+' · "
    "'+s.profile;$('latest').textContent=s.latest?'Phiên bản trên máy chủ: "
    "'+s.latest:'';$('notes').textContent=s.notes||'';$('status').textContent="
    "s.message;$('progress').value=s.progress;$('check').disabled=s.busy||"
    "sending;$('install').disabled=s.busy||sending||!s.available;if(s.state==='"
    "rebooting')$('status').textContent='Đã ghi và kiểm tra xong. ESP đang "
    "khởi động lại; đợi khoảng 30 giây rồi tải lại "
    "trang.'}catch(e){$('status').textContent='Đang chờ ESP kết nối "
    "lại…'}}async function send(action){sending=true;try{const r=await "
    "fetch('/api/update',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify({action})}),j=await "
    "r.json();$('status').textContent=j.message;if(!r.ok)alert(j.message)}"
    "catch(e){$('status').textContent=e.message}finally{sending=false;poll()}}$"
    "('check').onclick=()=>send('check');$('install').onclick=()=>{if(confirm('"
    "Cập nhật firmware đúng model và khởi động lại "
    "ESP?'))send('install')};setInterval(poll,1500);poll();</script></html>";
static void reply(airprint_http_t *h, const char *code, const char *text) {
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "message", text);
  char *s = cJSON_PrintUnformatted(j);
  if (s)
    airprint_http_send_ex(h, code, "application/json",
                          "Cache-Control: no-store\r\n", s, strlen(s));
  else
    airprint_http_send(h, "500 Internal Server Error", "text/plain", "Memory",
                       6);
  free(s);
  cJSON_Delete(j);
}
static const char *str(cJSON *j, const char *k) {
  cJSON *x = cJSON_GetObjectItemCaseSensitive(j, k);
  return cJSON_IsString(x) ? x->valuestring : NULL;
}
static esp_http_client_handle_t open_https(const char *url) {
  esp_http_client_config_t cfg = {.url = url,
                                  .crt_bundle_attach = esp_crt_bundle_attach,
                                  .timeout_ms = 20000,
                                  .buffer_size = 2048,
                                  .disable_auto_redirect = true};
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c)
    return NULL;
  if (esp_http_client_open(c, 0) != ESP_OK ||
      esp_http_client_fetch_headers(c) < 0 ||
      esp_http_client_get_status_code(c) != 200) {
    esp_http_client_cleanup(c);
    return NULL;
  }
  return c;
}
static bool manifest(char version[32], uint8_t hash[32], size_t *size) {
  esp_http_client_handle_t c =
      open_https(VP_CLOUD_BASE "firmware/" VP_PROFILE "/ota-manifest.json");
  if (!c)
    return false;
  char b[2049];
  size_t n = 0;
  int got;
  while (n < sizeof(b) - 1 &&
         (got = esp_http_client_read(c, b + n, sizeof(b) - 1 - n)) > 0)
    n += got;
  bool complete = esp_http_client_is_complete_data_received(c);
  esp_http_client_cleanup(c);
  if (!complete || n == sizeof(b) - 1)
    return false;
  b[n] = 0;
  cJSON *j = cJSON_ParseWithLength(b, n);
  const char *p = str(j, "profile"), *v = str(j, "version"),
             *file = str(j, "file"), *sha = str(j, "sha256");
  cJSON *bytes = cJSON_GetObjectItemCaseSensitive(j, "size");
  bool valid = p && v && v[0] && strlen(v) < 32 && file && sha &&
               strlen(sha) == 64 && cJSON_IsNumber(bytes) &&
               bytes->valuedouble > 0 && bytes->valuedouble <= 4194304 &&
               !strcmp(p, VP_PROFILE) && !strcmp(file, "firmware.bin");
  if (valid) {
    for (int i = 0; i < 32; i++) {
      char pair[3] = {sha[i * 2], sha[i * 2 + 1], 0}, *end;
      unsigned long h = strtoul(pair, &end, 16);
      if (*end || strspn(pair, "0123456789abcdefABCDEF") != 2) {
        valid = false;
        break;
      }
      hash[i] = h;
    }
    if (valid) {
      snprintf(version, 32, "%s", v);
      *size = (size_t)bytes->valuedouble;
    }
  }
  if (valid) {
    const char *text = str(j, "notes");
    taskENTER_CRITICAL(&lock);
    snprintf(notes, sizeof(notes), "%s", text ? text : "");
    taskEXIT_CRITICAL(&lock);
  }
  cJSON_Delete(j);
  return valid;
}
static void worker(void *arg) {
  bool install = (bool)(intptr_t)arg;
  uint8_t expected[32];
  char version[32];
  size_t size = 0;
  status("checking", "Đang kiểm tra phiên bản trên máy chủ…", 0);
  if (!wifi_connected() || !manifest(version, expected, &size)) {
    status("error",
           "Không tải được thông tin cập nhật. Kiểm tra Internet/thời gian hệ "
           "thống rồi thử lại.",
           0);
    goto done;
  }
  taskENTER_CRITICAL(&lock);
  snprintf(latest, sizeof(latest), "%s", version);
  taskEXIT_CRITICAL(&lock);
  if (!install) {
    status("checked",
           strcmp(version, esp_app_get_description()->version)
               ? "Có phiên bản khác trên máy chủ. Xem phiên bản trước khi cập "
                 "nhật."
               : "Bạn đang dùng phiên bản hiện tại.",
           0);
    goto done;
  }
  if (!strcmp(version, esp_app_get_description()->version)) {
    status("checked", "Bạn đang dùng đúng phiên bản trên máy chủ.", 0);
    goto done;
  }
  if (usb_printer_job_active() || wifi_setup_pending()) {
    status("error", "Máy đang in hoặc cấu hình Wi-Fi; hãy thử lại khi rảnh.",
           0);
    goto done;
  }
  const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
  if (!part || size > part->size) {
    status("error", "Bản cập nhật vượt quá phân vùng firmware.", 0);
    goto done;
  }
  esp_http_client_handle_t c =
      open_https(VP_CLOUD_BASE "firmware/" VP_PROFILE "/firmware.bin");
  if (!c) {
    status("error", "Không tải được firmware qua HTTPS.", 0);
    goto done;
  }
  uint8_t *buf = malloc(4096);
  esp_ota_handle_t ota = 0;
  bool ota_open = false, ok = false;
  size_t count = 0;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  if (!buf || mbedtls_sha256_starts(&sha, 0))
    goto cleanup;
  // Validate app description before erasing the inactive OTA slot.
  // Header+segment+descriptor.
  size_t first = sizeof(esp_image_header_t) +
                 sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
  while (count < first) {
    int got = esp_http_client_read(c, (char *)buf + count, first - count);
    if (got <= 0)
      goto cleanup;
    count += got;
  }
  esp_app_desc_t desc;
  memcpy(&desc,
         buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
         sizeof(desc));
  const esp_app_desc_t *running = esp_app_get_description();
  if (buf[0] != ESP_IMAGE_HEADER_MAGIC ||
      desc.magic_word != ESP_APP_DESC_MAGIC_WORD ||
      memcmp(desc.project_name, running->project_name,
             sizeof(desc.project_name)) ||
      strncmp(desc.version, version, sizeof(desc.version)))
    goto cleanup;
  if (esp_ota_begin(part, size, &ota) != ESP_OK)
    goto cleanup;
  ota_open = true;
  if (mbedtls_sha256_update(&sha, buf, count) ||
      esp_ota_write(ota, buf, count) != ESP_OK)
    goto cleanup;
  status("downloading", "Đang tải và ghi firmware…", (int)(count * 100 / size));
  while (count < size) {
    size_t need = size - count;
    if (need > 4096)
      need = 4096;
    int got = esp_http_client_read(c, (char *)buf, need);
    if (got <= 0)
      goto cleanup;
    if (mbedtls_sha256_update(&sha, buf, got) ||
        esp_ota_write(ota, buf, got) != ESP_OK)
      goto cleanup;
    count += got;
    taskENTER_CRITICAL(&lock);
    progress = (int)(count * 100 / size);
    taskEXIT_CRITICAL(&lock);
    vTaskDelay(1);
  }
  uint8_t extra;
  int tail = esp_http_client_read(c, (char *)&extra, 1);
  uint8_t digest[32];
  if (tail != 0 || !esp_http_client_is_complete_data_received(c) ||
      mbedtls_sha256_finish(&sha, digest) || memcmp(digest, expected, 32))
    goto cleanup;
  {
    esp_err_t e = esp_ota_end(ota);
    ota_open = false;
    if (e != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK)
      goto cleanup;
  }
  ok = true;
cleanup:
  if (ota_open)
    esp_ota_abort(ota);
  mbedtls_sha256_free(&sha);
  free(buf);
  esp_http_client_cleanup(c);
  if (ok) {
    status("rebooting", "Đã kiểm tra SHA-256 và ghi xong. Đang khởi động lại…",
           100);
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
  }
  status("error",
         "Cập nhật thất bại hoặc sai model/checksum. Thiết bị tiếp tục dùng "
         "firmware hiện tại.",
         0);
done:
  taskENTER_CRITICAL(&lock);
  busy = false;
  taskEXIT_CRITICAL(&lock);
  vTaskDelete(NULL);
}
bool cloud_update_http(airprint_http_t *h, const char *method,
                       const char *path) {
  if (!strcmp(path, "/update") && !strcmp(method, "GET")) {
    airprint_http_send(h, "200 OK", "text/html; charset=utf-8", page,
                       sizeof(page) - 1);
    return true;
  }
  if (strcmp(path, "/api/update"))
    return false;
  if (!strcmp(method, "GET")) {
    char st[sizeof(state)], msg[sizeof(message)], ver[sizeof(latest)],
        note[sizeof(notes)];
    bool b;
    int pct;
    taskENTER_CRITICAL(&lock);
    memcpy(st, state, sizeof(st));
    memcpy(msg, message, sizeof(msg));
    memcpy(ver, latest, sizeof(ver));
    memcpy(note, notes, sizeof(note));
    b = busy;
    pct = progress;
    taskEXIT_CRITICAL(&lock);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "current", esp_app_get_description()->version);
    cJSON_AddStringToObject(j, "profile", VP_PROFILE);
    cJSON_AddStringToObject(j, "state", st);
    cJSON_AddStringToObject(j, "message", msg);
    cJSON_AddStringToObject(j, "latest", ver);
    cJSON_AddStringToObject(j, "notes", note);
    cJSON_AddBoolToObject(j, "busy", b);
    cJSON_AddNumberToObject(j, "progress", pct);
    cJSON_AddBoolToObject(j, "available",
                          ver[0] &&
                              strcmp(ver, esp_app_get_description()->version));
    char *s = cJSON_PrintUnformatted(j);
    if (s)
      airprint_http_send_ex(h, "200 OK", "application/json",
                            "Cache-Control: no-store\r\n", s, strlen(s));
    free(s);
    cJSON_Delete(j);
    return true;
  }
  if (strcmp(method, "POST")) {
    reply(h, "405 Method Not Allowed", "Chỉ hỗ trợ GET/POST.");
    return true;
  }
  char b[96];
  int n = airprint_http_read_body(h, b, sizeof(b) - 1);
  if (n <= 0 || (size_t)n >= sizeof(b) - 1) {
    reply(h, "400 Bad Request", "Dữ liệu không hợp lệ.");
    return true;
  }
  cJSON *j = cJSON_ParseWithLength(b, n);
  const char *action = str(j, "action");
  bool check = action && !strcmp(action, "check"),
       install = action && !strcmp(action, "install");
  cJSON_Delete(j);
  if (!check && !install) {
    reply(h, "400 Bad Request", "Chọn check hoặc install.");
    return true;
  }
  if (install && !cloud_update_allowed()) {
    reply(h, "403 Forbidden",
          "Giữ BOOT 5 giây trên mạch, rồi bấm Cập nhật trong 2 phút.");
    return true;
  }
  taskENTER_CRITICAL(&lock);
  bool was_busy = busy;
  if (!busy)
    busy = true;
  taskEXIT_CRITICAL(&lock);
  if (was_busy) {
    reply(h, "409 Conflict", "Đang xử lý một yêu cầu cập nhật.");
    return true;
  }
  if (usb_printer_job_active() || wifi_setup_pending() || !wifi_connected()) {
    taskENTER_CRITICAL(&lock);
    busy = false;
    taskEXIT_CRITICAL(&lock);
    reply(h, "409 Conflict", "Cần Wi-Fi có Internet và máy đang rảnh.");
    return true;
  }
  if (xTaskCreate(worker, "cloud_ota", 8192, (void *)(intptr_t)install, 3,
                  NULL) != pdPASS) {
    taskENTER_CRITICAL(&lock);
    busy = false;
    taskEXIT_CRITICAL(&lock);
    reply(h, "503 Service Unavailable", "Không đủ bộ nhớ để bắt đầu.");
    return true;
  }
  reply(h, "202 Accepted",
        install ? "Đã bắt đầu cập nhật." : "Đang kiểm tra phiên bản.");
  return true;
}
