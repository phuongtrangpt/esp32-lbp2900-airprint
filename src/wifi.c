#include "wifi.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "local_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/task.h"

static const char *TAG = "wifi_sta";

static volatile bool s_connected;
static int s_retries;
static esp_netif_t *s_netif;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        s_connected = false;
        if (s_retries++ % 10 == 0)
            ESP_LOGW(TAG, "disconnected (reason %d), retrying", d->reason);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "connected, IP " IPSTR, IP2STR(&e->ip_info.ip));
        s_retries = 0;
        s_connected = true;
    }
}

// First-boot provisioning over the UART/COM connector. Credentials stay in NVS;
// they are never printed, included in a distributed firmware, or sent to GitHub.
static int serial_line(char *out, size_t cap)
{
    size_t n = 0;
    bool overflow = false;
    while (1) {
        int c = getchar();
        if (c == EOF) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (c == '\r') continue;
        if (c == '\n') { out[n] = 0; return overflow ? -1 : (int)n; }
        if (c == 8 || c == 127) { if (n) n--; continue; }
        if (n + 1 < cap) out[n++] = (char)c; else overflow = true;
    }
}
static esp_err_t credentials(char *ssid, size_t sn, char *password, size_t pn)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open("lbp2900", NVS_READWRITE, &h), TAG, "NVS open");
    size_t a = sn, b = pn;
    bool saved = nvs_get_str(h, "ssid", ssid, &a) == ESP_OK &&
                 nvs_get_str(h, "password", password, &b) == ESP_OK && ssid[0];
    if (!saved && WIFI_SSID[0]) {
        snprintf(ssid, sn, "%s", WIFI_SSID);
        snprintf(password, pn, "%s", WIFI_PASSWORD);
        nvs_close(h);
        return ESP_OK;
    }
    if (!saved) {
        puts("\nLBP2900 first setup. Use COM/UART at 115200 baud, LF line endings.");
        puts("WiFi 2.4 GHz SSID (max 32 UTF-8 bytes):");
        int n;
        do { n = serial_line(ssid, sn); } while (n <= 0 || n > 32);
        puts("WiFi password (8-63 characters, or empty for an open network):");
        do { n = serial_line(password, pn); } while (n < 0 || (n != 0 && (n < 8 || n > 63)));
        esp_err_t err = nvs_set_str(h, "ssid", ssid);
        if (err == ESP_OK) err = nvs_set_str(h, "password", password);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
        if (err != ESP_OK) return err;
        puts("WiFi settings saved. Connecting...");
        return ESP_OK;
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t wifi_start(const char *hostname)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs");
    char ssid[33] = {0}, password[65] = {0};
    ESP_RETURN_ON_ERROR(credentials(ssid, sizeof(ssid), password, sizeof(password)), TAG, "credentials");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    s_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_netif, hostname);

    // the driver's own info logs include the SSID; keep warnings only
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);

    wifi_config_t cfg = {0};
    memcpy(cfg.sta.ssid, ssid, strlen(ssid));
    memcpy(cfg.sta.password, password, strlen(password));
    cfg.sta.threshold.authmode = strlen(password) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "config");
    // no modem sleep: a sleeping station answers mDNS / IPP slowly
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");

    // real wall clock for IPP date-time attributes
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);
    ESP_LOGI(TAG, "connecting as %s", hostname);
    return ESP_OK;
}

bool wifi_connected(void)
{
    return s_connected;
}

bool wifi_get_ip(char *buf, size_t n)
{
    esp_netif_ip_info_t ip;
    if (!s_connected || !s_netif || esp_netif_get_ip_info(s_netif, &ip) != ESP_OK) {
        if (n)
            buf[0] = 0;
        return false;
    }
    snprintf(buf, n, IPSTR, IP2STR(&ip.ip));
    return true;
}

int wifi_rssi(void)
{
    int rssi = 0;
    if (!s_connected || esp_wifi_sta_get_rssi(&rssi) != ESP_OK)
        return 0;
    return rssi;
}
