// WiFi station with NVS credentials and Improv Serial browser setup.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

// Starts Wi-Fi and browser provisioning in the background.
esp_err_t wifi_start(const char *hostname);
bool wifi_connected(void);
// The station's IPv4 address as text; false (and "") while not connected.
bool wifi_get_ip(char *buf, size_t n);
// Signal of the access point in dBm, 0 when unknown.
int wifi_rssi(void);

bool wifi_get_network(char *mask,size_t m,char *gateway,size_t g);

#include "network_config.h"
#include "airprint.h"
// Queued to the Wi-Fi worker; credentials are saved only after connection succeeds.
esp_err_t wifi_submit_setup(const char *ssid,const char *password,const network_config_t *cfg,bool serial);
void wifi_request_setup(void);
bool wifi_setup_active(void);
bool wifi_setup_http(airprint_http_t *h,const char *method,const char *path);

const char *wifi_setup_name(void);
int wifi_setup_error(void);
bool wifi_setup_pending(void);
esp_err_t wifi_setup_next_boot(void);
esp_err_t wifi_handover(void);
void setup_dns_start(void);
