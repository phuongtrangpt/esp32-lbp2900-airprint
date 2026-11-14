// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct improv_serial improv_serial_t;
struct improv_serial {
    uint8_t packet[265];
    size_t used;
    void (*write)(void *ctx, const uint8_t *data, size_t len);
    void (*command)(void *ctx, uint8_t command, const uint8_t *data, size_t len);
    void *ctx;
};
// Returns true when a complete frame (valid or invalid) was consumed.
bool improv_serial_feed(improv_serial_t *s, uint8_t byte);
void improv_serial_state(improv_serial_t *s, uint8_t state);
void improv_serial_error(improv_serial_t *s, uint8_t error);
bool improv_serial_result(improv_serial_t *s, uint8_t command, const char *const *strings, size_t count);
bool improv_serial_wifi(const uint8_t *data, size_t len, char ssid[33], char password[65]);
