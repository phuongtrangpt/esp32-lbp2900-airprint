#include "wifi.h"
#include "brand.h"
#include "cloud_update.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "improv_serial.h"
#include "local_config.h"
#include "network_settings.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "usb_printer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "wifi_sta";
static volatile bool s_connected, s_has_credentials;
static int s_retries;
static esp_netif_t *s_netif;
static QueueHandle_t s_setup_queue;
static volatile bool s_ap, s_force_setup, s_handover;
static portMUX_TYPE s_setup_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_configuration_pending;
static bool s_scanning;
static volatile int s_setup_error;
static volatile bool s_initial_credentials;
static TickType_t s_offline_at, s_ap_close_at, s_handover_at;
static volatile TickType_t s_force_until;
static char s_ap_name[33];
typedef struct {
  char ssid[33], password[65];
  network_config_t network;
  bool serial;
} setup_request_t;
static setup_request_t s_request;
static esp_err_t apply_network(const network_config_t *n) {
  esp_err_t e = esp_netif_dhcpc_stop(s_netif);
  if (e != ESP_OK && e != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
    return e;
  esp_netif_ip_info_t info = {0};
  if (n->enabled) {
    info.ip.addr = n->ip;
    info.netmask.addr = n->mask;
    info.gw.addr = n->gateway;
  }
  e = esp_netif_set_ip_info(s_netif, &info);
  if (e != ESP_OK)
    return e;
  if (!n->enabled) {
    e = esp_netif_dhcpc_start(s_netif);
    return e == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED ? ESP_OK : e;
  }
  esp_netif_dns_info_t dns = {0};
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = n->dns;
  return esp_netif_set_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &dns);
}
static improv_serial_t s_improv;
// Only the serial task owns provisioning data and emits Improv packets.
static volatile bool s_pending;
static TickType_t s_started;
static char s_ssid[33], s_password[65];
static bool s_legacy_password;
static char s_legacy_ssid[33];

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    if (s_has_credentials)
      esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const wifi_event_sta_disconnected_t *d = data;
    s_connected = false;
    taskENTER_CRITICAL(&s_setup_lock);
    bool scanning = s_scanning;
    taskEXIT_CRITICAL(&s_setup_lock);
    if (s_has_credentials && !scanning) {
      if (s_retries++ % 10 == 0)
        ESP_LOGW(TAG, "disconnected (reason %d), retrying", d->reason);
      esp_wifi_connect();
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t *e = data;
    if (s_pending) {
      wifi_ap_record_t ap;
      if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
          strcmp((const char *)ap.ssid, s_ssid))
        return;
    }
    ESP_LOGI(TAG, "connected, IP " IPSTR, IP2STR(&e->ip_info.ip));
    s_retries = 0;
    s_connected = true;
  }
}
static esp_err_t stored_credentials(bool save, char ssid[33],
                                    char password[65]) {
  nvs_handle_t h;
  esp_err_t e = nvs_open("lbp2900", save ? NVS_READWRITE : NVS_READONLY, &h);
  if (e != ESP_OK)
    return e;
  if (save) {
    e = nvs_set_str(h, "ssid", ssid);
    if (e == ESP_OK)
      e = nvs_set_str(h, "password", password);
    if (e == ESP_OK)
      e = nvs_commit(h);
  } else {
    size_t a = 33, b = 65;
    e = nvs_get_str(h, "ssid", ssid, &a);
    if (e == ESP_OK)
      e = nvs_get_str(h, "password", password, &b);
  }
  nvs_close(h);
  return e;
}
static void serial_write(void *ctx, const uint8_t *p, size_t n) {
  // Raw UART output preserves binary bytes (no CR/LF translation). Keep a
  // complete frame together relative to normal stdout/ESP log messages.
  flockfile(stdout);
  uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM, p, n);
  funlockfile(stdout);
}
static uint8_t provision_state(void) {
  return s_connected ? 4 : (s_pending ? 3 : 2);
}
static void result_url(uint8_t command) {
  char ip[24], url[48];
  if (wifi_get_ip(ip, sizeof(ip))) {
    snprintf(url, sizeof(url), "http://%s/", ip);
    const char *strings[] = {url};
    improv_serial_result(&s_improv, command, strings, 1);
  } else
    improv_serial_result(&s_improv, command, NULL, 0);
}
static esp_err_t apply_credentials(const char *ssid, const char *password) {
  s_has_credentials = false;
  s_connected = false;
  esp_wifi_disconnect();
  wifi_config_t cfg = {0};
  memcpy(cfg.sta.ssid, ssid, strlen(ssid));
  memcpy(cfg.sta.password, password, strlen(password));
  cfg.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  cfg.sta.pmf_cfg.capable = true;
  esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &cfg);
  if (e == ESP_OK) {
    s_has_credentials = true;
    e = esp_wifi_connect();
  }
  return e;
}
esp_err_t wifi_scan_networks(wifi_ap_record_t *aps, uint16_t *count) {
  taskENTER_CRITICAL(&s_setup_lock);
  bool busy = s_scanning || s_configuration_pending;
  if (!busy)
    s_scanning = true;
  taskEXIT_CRITICAL(&s_setup_lock);
  if (busy)
    return ESP_ERR_INVALID_STATE;

  // Reconnecting to an old SSID can otherwise block scans indefinitely.
  bool reconnecting = !s_connected && s_has_credentials;
  if (reconnecting)
    esp_wifi_disconnect();
  wifi_scan_config_t scan = {0};
  esp_err_t err = esp_wifi_scan_start(&scan, true);
  if (reconnecting && err == ESP_ERR_WIFI_STATE) {
    vTaskDelay(pdMS_TO_TICKS(150));
    err = esp_wifi_scan_start(&scan, true);
  }
  if (err == ESP_OK) {
    err = esp_wifi_scan_get_ap_records(count, aps);
    if (err != ESP_OK)
      esp_wifi_clear_ap_list();
  }

  taskENTER_CRITICAL(&s_setup_lock);
  s_scanning = false;
  taskEXIT_CRITICAL(&s_setup_lock);
  if (!s_connected && s_has_credentials && !wifi_setup_pending())
    esp_wifi_connect();
  return err;
}

static void scan_networks(void) {
  uint16_t count = 16;
  wifi_ap_record_t *aps = calloc(count, sizeof(*aps));
  if (aps && wifi_scan_networks(aps, &count) == ESP_OK)
    for (unsigned i = 0; i < count; i++) {
      if (!aps[i].ssid[0])
        continue;
      char signal[12];
      snprintf(signal, sizeof(signal), "%d", aps[i].rssi);
      const char *strings[] = {(char *)aps[i].ssid, signal,
                               aps[i].authmode == WIFI_AUTH_OPEN ? "NO"
                                                                 : "YES"};
      improv_serial_result(&s_improv, 4, strings, 3);
    }
  free(aps);
  improv_serial_result(&s_improv, 4, NULL, 0);
}

static void command(void *ctx, uint8_t cmd, const uint8_t *p, size_t n) {
  if (cmd == 1) {
    char ssid[33], password[65];
    if (!improv_serial_wifi(p, n, ssid, password)) {
      improv_serial_error(&s_improv, 1);
      return;
    }
    network_config_t network;
    network_settings_load(&network);
    if (wifi_submit_setup(ssid, password, &network, true) != ESP_OK)
      improv_serial_error(&s_improv, 3);
    memset(password, 0, sizeof(password));
  } else if (n)
    improv_serial_error(&s_improv, 1);
  else if (cmd == 2) {
    improv_serial_state(&s_improv, provision_state());
    if (s_connected)
      result_url(2);
  } else if (cmd == 3) {
    const char *strings[] = {PRINTER_NAME, esp_app_get_description()->version,
                             "ESP32-S3 N16R8", PRINTER_NAME};
    improv_serial_result(&s_improv, 3, strings, 4);
  } else if (cmd == 4)
    scan_networks();
  else
    improv_serial_error(&s_improv, 2);
}
static void legacy_line(const char *line) {
  // Keep the old serial setup available. "setup" restarts its two prompts.
  if (!strcmp(line, "setup")) {
    s_has_credentials = false;
    s_connected = false;
    esp_wifi_disconnect();
    s_legacy_password = false;
    s_legacy_ssid[0] = 0;
    puts("WiFi SSID:");
    return;
  }
  if (s_pending || s_has_credentials)
    return;
  size_t n = strlen(line);
  if (!s_legacy_password) {
    if (n == 0 || n > 32) {
      puts("WiFi SSID: 1-32 UTF-8 bytes");
      return;
    }
    memcpy(s_legacy_ssid, line, n + 1);
    s_legacy_password = true;
    puts("WiFi password (8-63 characters, or empty):");
  } else {
    if (n > 63 || (n && n < 8)) {
      puts("WiFi password: 8-63 characters, or empty");
      return;
    }
    uint8_t data[98];
    size_t a = strlen(s_legacy_ssid);
    data[0] = a;
    memcpy(data + 1, s_legacy_ssid, a);
    data[a + 1] = n;
    memcpy(data + a + 2, line, n);
    command(NULL, 1, data, a + n + 2);
    memset(data, 0, sizeof(data));
    s_legacy_password = false;
  }
}
static void serial_task(void *arg) {
  uint8_t byte;
  char line[100] = {0};
  size_t used = 0;
  bool overflow = false;
  TickType_t last = xTaskGetTickCount();
  if (!s_has_credentials)
    puts("ESP 32: open the web installer to set WiFi, or enter SSID then "
         "password here.");
  for (;;) {
    if (uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &byte, 1,
                        pdMS_TO_TICKS(20)) == 1) {
      TickType_t now = xTaskGetTickCount();
      if (now - last > pdMS_TO_TICKS(1000))
        s_improv.used = 0;
      last = now;
      bool frame = improv_serial_feed(&s_improv, byte);
      if (frame) {
        used = 0;
        overflow = false;
        memset(line, 0, sizeof(line));
      } else if (byte == '\n' && !s_improv.used) {
        if (!overflow) {
          line[used] = 0;
          legacy_line(line);
        }
        used = 0;
        overflow = false;
        memset(line, 0, sizeof(line));
      } else if (byte == 8 || byte == 127) {
        if (used)
          used--;
      } else if (byte >= 32) {
        if (used + 1 < sizeof(line))
          line[used++] = (char)byte;
        else
          overflow = true;
      }
    }
    if (!s_pending && xQueueReceive(s_setup_queue, &s_request, 0) == pdTRUE) {
      s_setup_error = 0;
      s_pending = true;
      s_started = xTaskGetTickCount();
      memcpy(s_ssid, s_request.ssid, sizeof(s_ssid));
      memcpy(s_password, s_request.password, sizeof(s_password));
      memset(s_request.password, 0, sizeof(s_request.password));
      s_has_credentials = false;
      s_connected = false;
      esp_wifi_disconnect();
      if (s_request.serial)
        improv_serial_state(&s_improv, 3);
      if (apply_network(&s_request.network) != ESP_OK ||
          apply_credentials(s_ssid, s_password) != ESP_OK) {
        s_setup_error = 3;
        s_started = xTaskGetTickCount() - pdMS_TO_TICKS(30001);
      }
    }
    if (s_pending && s_connected) {
      esp_err_t e = network_settings_save(&s_request.network);
      if (e == ESP_OK)
        e = stored_credentials(true, s_ssid, s_password);
      s_setup_error = e == ESP_OK ? 0 : 5;
      if (s_request.serial) {
        improv_serial_error(&s_improv, e == ESP_OK ? 0 : 0xff);
        improv_serial_state(&s_improv, 4);
        result_url(1);
      }
      s_pending = false;
      taskENTER_CRITICAL(&s_setup_lock);
      s_configuration_pending = false;
      taskEXIT_CRITICAL(&s_setup_lock);
      memset(s_password, 0, sizeof(s_password));
      if (e == ESP_OK) {
        s_initial_credentials = true;
        s_force_until = 0;
        s_ap_close_at = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
      } else {
        s_force_until = xTaskGetTickCount() + pdMS_TO_TICKS(600000);
      }
    } else if (s_pending &&
               xTaskGetTickCount() - s_started > pdMS_TO_TICKS(30000)) {
      s_pending = false;
      s_has_credentials = false;
      esp_wifi_disconnect();
      s_setup_error = 3;
      memset(s_password, 0, sizeof(s_password));
      if (s_request.serial) {
        improv_serial_error(&s_improv, 3);
        improv_serial_state(&s_improv, 2);
      }
      // Keep the previous saved settings; a failed attempt must not erase them.
      char old_ssid[33] = {0}, old_password[65] = {0};
      network_config_t old_network;
      network_settings_load(&old_network);
      if (stored_credentials(false, old_ssid, old_password) == ESP_OK) {
        apply_network(&old_network);
        apply_credentials(old_ssid, old_password);
      }
      memset(old_password, 0, sizeof(old_password));
      s_force_until = xTaskGetTickCount() + pdMS_TO_TICKS(600000);
      taskENTER_CRITICAL(&s_setup_lock);
      s_configuration_pending = false;
      taskEXIT_CRITICAL(&s_setup_lock);
    }
    TickType_t now = xTaskGetTickCount();
    if (s_force_setup) {
      s_force_setup = false;
      s_force_until = now + pdMS_TO_TICKS(600000);
    }
    if (s_connected)
      s_offline_at = now;
    bool forced = s_force_until && (int32_t)(s_force_until - now) > 0;
    bool want = !s_initial_credentials && !s_connected;
    want = want || forced ||
           (!s_connected && now - s_offline_at > pdMS_TO_TICKS(60000));
    if (s_ap_close_at && (int32_t)(s_ap_close_at - now) > 0)
      want = true;
    if (want && !s_ap && !cloud_update_busy()) {
      if (esp_wifi_set_mode(WIFI_MODE_APSTA) == ESP_OK) {
        s_ap = true;
        ESP_LOGI(TAG, "Setup hotspot %s at " VP_SETUP_IP, s_ap_name);
      }
    } else if (!want && s_ap && !s_pending) {
      if (esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK)
        s_ap = false;
    }
    if (s_handover && now - s_handover_at > pdMS_TO_TICKS(2000) &&
        !usb_printer_job_active() && !cloud_update_busy())
      esp_restart();
  }
}
esp_err_t wifi_start(const char *hostname) {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS");
    err = nvs_flash_init();
  }
  ESP_RETURN_ON_ERROR(err, TAG, "NVS");
  char ssid[33] = {0}, password[65] = {0};
  if (stored_credentials(false, ssid, password) != ESP_OK) {
    snprintf(ssid, sizeof(ssid), "%s", WIFI_SSID);
    snprintf(password, sizeof(password), "%s", WIFI_PASSWORD);
  }
  s_has_credentials = ssid[0] != 0;
  s_initial_credentials = s_has_credentials;
  s_offline_at = xTaskGetTickCount();
  s_setup_queue = xQueueCreate(1, sizeof(setup_request_t));
  if (!s_setup_queue)
    return ESP_ERR_NO_MEM;
  ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
  ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
  s_netif = esp_netif_create_default_wifi_sta();
  esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
  esp_netif_dns_info_t ap_dns = {0};
  ap_dns.ip.type = ESP_IPADDR_TYPE_V4;
  esp_netif_ip_info_t ap_ip;
  esp_netif_get_ip_info(ap_netif, &ap_ip);
  ap_dns.ip.u_addr.ip4.addr = ap_ip.ip.addr;
  esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &ap_dns);
  nvs_handle_t setup_nvs;
  uint8_t force = 0;
  if (nvs_open("vp_setup", NVS_READWRITE, &setup_nvs) == ESP_OK) {
    nvs_get_u8(setup_nvs, "force", &force);
    nvs_erase_key(setup_nvs, "force");
    nvs_commit(setup_nvs);
    nvs_close(setup_nvs);
  }
  if (force)
    s_force_setup = true;
  esp_netif_set_hostname(s_netif, hostname);
  network_config_t network;
  network_settings_load(&network);
  if (network.enabled) {
    esp_netif_ip_info_t ip = {.ip.addr = network.ip,
                              .netmask.addr = network.mask,
                              .gw.addr = network.gateway};
    esp_err_t stopped = esp_netif_dhcpc_stop(s_netif);
    if (stopped != ESP_OK && stopped != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)
      return stopped;
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(s_netif, &ip), TAG, "static IP");
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = network.dns;
    ESP_RETURN_ON_ERROR(
        esp_netif_set_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &dns), TAG, "DNS");
    ESP_LOGI(TAG, "static IPv4 configured");
  }
  esp_log_level_set("wifi", ESP_LOG_WARN);
  esp_log_level_set("wifi_init", ESP_LOG_WARN);
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "WiFi init");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL),
      TAG, "WiFi event");
  ESP_RETURN_ON_ERROR(
      esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL),
      TAG, "IP event");
  wifi_config_t cfg = {0};
  memcpy(cfg.sta.ssid, ssid, strlen(ssid));
  memcpy(cfg.sta.password, password, strlen(password));
  cfg.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
  cfg.sta.pmf_cfg.capable = true;
  ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG,
                      "WiFi storage");
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "WiFi mode");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG,
                      "WiFi config");
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(s_ap_name, sizeof(s_ap_name), VP_AP_PREFIX "-%02X%02X", mac[4],
           mac[5]);
  wifi_config_t ap = {0};
  size_t ap_len = strlen(s_ap_name);
  if (ap_len > sizeof(ap.ap.ssid))
    return ESP_ERR_INVALID_SIZE;
  memcpy(ap.ap.ssid, s_ap_name, ap_len);
  ap.ap.ssid_len = strlen(s_ap_name);
  ap.ap.channel = 1;
  ap.ap.max_connection = 2;
  // Setup network is open; it closes automatically after Wi-Fi provisioning.
  ap.ap.authmode = WIFI_AUTH_OPEN;
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "AP+STA");
  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "setup AP");
  if (s_initial_credentials && !force)
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "station mode");
  else
    s_ap = true;
  esp_wifi_set_ps(WIFI_PS_NONE);
  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WiFi start");
  memset(password, 0, sizeof(password));
  if (!uart_is_driver_installed(CONFIG_ESP_CONSOLE_UART_NUM))
    ESP_RETURN_ON_ERROR(
        uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 1024, 0, 0, NULL, 0),
        TAG, "serial provisioning");
  s_improv = (improv_serial_t){.write = serial_write, .command = command};
  if (xTaskCreate(serial_task, "improv", 4096, NULL, 3, NULL) != pdPASS)
    return ESP_ERR_NO_MEM;
  esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&sntp);
  ESP_LOGI(TAG, "WiFi station %s; browser provisioning ready", hostname);
  return ESP_OK;
}
bool wifi_connected(void) { return s_connected; }
bool wifi_get_ip(char *buf, size_t n) {
  esp_netif_ip_info_t ip;
  if (!s_connected || !s_netif ||
      esp_netif_get_ip_info(s_netif, &ip) != ESP_OK) {
    if (n)
      buf[0] = 0;
    return false;
  }
  snprintf(buf, n, IPSTR, IP2STR(&ip.ip));
  return true;
}
int wifi_rssi(void) {
  int rssi = 0;
  return s_connected && esp_wifi_sta_get_rssi(&rssi) == ESP_OK ? rssi : 0;
}

bool wifi_get_network(char *mask, size_t m, char *gateway, size_t g) {
  esp_netif_ip_info_t ip;
  if (!s_netif || esp_netif_get_ip_info(s_netif, &ip) != ESP_OK)
    return false;
  snprintf(mask, m, IPSTR, IP2STR(&ip.netmask));
  snprintf(gateway, g, IPSTR, IP2STR(&ip.gw));
  return true;
}

bool wifi_current_ipv4_config(network_config_t *cfg) {
  esp_netif_ip_info_t info;
  if (!s_connected || !s_netif ||
      esp_netif_get_ip_info(s_netif, &info) != ESP_OK)
    return false;

  char ip[16], mask[16], gateway[16], dns[16] = "1.1.1.1";
  snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
  snprintf(mask, sizeof(mask), IPSTR, IP2STR(&info.netmask));
  snprintf(gateway, sizeof(gateway), IPSTR, IP2STR(&info.gw));
  esp_netif_dns_info_t current_dns;
  if (esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &current_dns) == ESP_OK &&
      current_dns.ip.type == ESP_IPADDR_TYPE_V4 && current_dns.ip.u_addr.ip4.addr)
    snprintf(dns, sizeof(dns), IPSTR, IP2STR(&current_dns.ip.u_addr.ip4));
  return s_connected && network_config_parse(cfg, ip, mask, gateway, dns);
}

void wifi_request_setup(void) { s_force_setup = true; }
bool wifi_setup_active(void) { return s_ap; }
const char *wifi_setup_name(void) { return s_ap_name; }
int wifi_setup_error(void) { return s_setup_error; }
bool wifi_setup_pending(void) {
  taskENTER_CRITICAL(&s_setup_lock);
  bool b = s_configuration_pending;
  taskEXIT_CRITICAL(&s_setup_lock);
  return b;
}
esp_err_t wifi_setup_next_boot(void) {
  nvs_handle_t h;
  esp_err_t e = nvs_open("vp_setup", NVS_READWRITE, &h);
  if (e == ESP_OK) {
    e = nvs_set_u8(h, "force", 1);
    if (e == ESP_OK)
      e = nvs_commit(h);
    nvs_close(h);
  }
  return e;
}
esp_err_t wifi_submit_setup(const char *ssid, const char *password,
                            const network_config_t *cfg, bool serial) {
  if (!ssid || !password || !cfg || !ssid[0] || strlen(ssid) > 32 ||
      strlen(password) > 63 || (password[0] && strlen(password) < 8))
    return ESP_ERR_INVALID_ARG;
  if (!s_setup_queue || s_pending || cloud_update_busy() ||
      usb_printer_job_active())
    return ESP_ERR_INVALID_STATE;
  if (!serial &&
      (!s_ap || (s_initial_credentials &&
                 (!s_force_until ||
                  (int32_t)(s_force_until - xTaskGetTickCount()) <= 0))))
    return ESP_ERR_INVALID_STATE;
  taskENTER_CRITICAL(&s_setup_lock);
  bool already = s_configuration_pending || s_scanning;
  if (!already)
    s_configuration_pending = true;
  taskEXIT_CRITICAL(&s_setup_lock);
  if (already)
    return ESP_ERR_INVALID_STATE;
  if (cloud_update_busy() || usb_printer_job_active()) {
    taskENTER_CRITICAL(&s_setup_lock);
    s_configuration_pending = false;
    taskEXIT_CRITICAL(&s_setup_lock);
    return ESP_ERR_INVALID_STATE;
  }
  s_setup_error = 0; // Clear an earlier failed attempt before exposing queued status.
  setup_request_t r = {.network = *cfg, .serial = serial};
  snprintf(r.ssid, sizeof(r.ssid), "%s", ssid);
  snprintf(r.password, sizeof(r.password), "%s", password);
  BaseType_t sent = xQueueSend(s_setup_queue, &r, 0);
  memset(r.password, 0, sizeof(r.password));
  if (sent != pdTRUE) {
    taskENTER_CRITICAL(&s_setup_lock);
    s_configuration_pending = false;
    taskEXIT_CRITICAL(&s_setup_lock);
  }
  return sent == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t wifi_handover(void) {
  nvs_handle_t h;
  esp_err_t e = nvs_open("lbp2900", NVS_READWRITE, &h);
  if (e == ESP_OK) {
    nvs_erase_key(h, "ssid");
    nvs_erase_key(h, "password");
    e = nvs_commit(h);
    nvs_close(h);
  }
  if (e == ESP_OK)
    e = network_settings_reset();
  if (e == ESP_OK)
    e = wifi_setup_next_boot();
  if (e == ESP_OK) {
    s_handover_at = xTaskGetTickCount();
    s_handover = true;
  }
  return e;
}
