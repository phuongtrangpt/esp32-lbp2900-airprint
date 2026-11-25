// Dedicated CAPT family builds do not include or load HP binary firmware.
#include "hp_firmware.h"
bool hp_firmware_http(airprint_http_t *h, const char *method, const char *path)
{ (void)h; (void)method; (void)path; return false; }
esp_err_t hp_firmware_load(void) { return ESP_ERR_NOT_SUPPORTED; }
