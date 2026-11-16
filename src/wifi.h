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
