// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>
#include "printer_driver.h"

typedef struct {
    printer_io_t io;
    jmp_buf guard;
    uint8_t *buffer;
    size_t used, rx_bytes;
    uint16_t command;
    uint64_t deadline_ms;
    const char *error;
} capt_transport_t;
void capt_bind(capt_transport_t *t);
void capt_deadline(capt_transport_t *t, unsigned timeout_ms);
_Noreturn void capt_fail(const char *message);

// Portable protocol diagnostics; ESP routes these to the shared log ring.
void capt_log(const char *fmt, ...);
