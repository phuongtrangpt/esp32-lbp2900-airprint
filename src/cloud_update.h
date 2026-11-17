#pragma once
#include "airprint.h"
#include <stdbool.h>
bool cloud_update_http(airprint_http_t *h, const char *method,
                       const char *path);
bool cloud_update_busy(void);
bool cloud_update_allowed(void);
void cloud_update_arm(void);
