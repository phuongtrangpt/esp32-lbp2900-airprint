#pragma once
#include "airprint.h"
#include "network_config.h"
#include "esp_err.h"
bool network_settings_http(airprint_http_t *h,const char *method,const char *path);
void network_settings_load(network_config_t *cfg);
esp_err_t network_settings_save(const network_config_t *cfg);
esp_err_t network_settings_reset(void);
