#pragma once
#include "airprint.h"
#include "esp_err.h"
bool hp_firmware_http(airprint_http_t *h, const char *method, const char *path);
// Caller already owns the USB job. Sends only a user-supplied HPDL image.
esp_err_t hp_firmware_load(void);
