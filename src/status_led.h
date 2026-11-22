// On-board WS2812 status LED (GPIO48), always dimmed to
// CONFIG_STATUS_LED_BRIGHTNESS percent.
#pragma once

#include "esp_err.h"

#include "status_led_pattern.h"

esp_err_t status_led_init(void);
void status_led_set(led_state_t state);
// Flashes blue a few times (IPP Identify-Printer), then resumes the state.
void status_led_identify(void);
