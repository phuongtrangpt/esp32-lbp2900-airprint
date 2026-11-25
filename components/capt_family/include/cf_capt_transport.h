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
    uint64_t deadline_ms, last_yield_ms;
    const char *error;
    bool cleaning_up;
} cf_capt_transport_t;
void cf_capt_bind(cf_capt_transport_t *t);
void cf_capt_deadline(cf_capt_transport_t *t, unsigned timeout_ms);
void cf_capt_current_deadline(unsigned timeout_ms);
void cf_capt_paper_out(bool waiting);
void cf_capt_restart_required(bool required);
_Noreturn void cf_capt_fail(const char *message);

// Portable protocol diagnostics; ESP routes these to the shared log ring.
void cf_capt_log(const char *fmt, ...);

void cf_capt_pause(unsigned ms);
struct printer_ops_s;
const struct printer_ops_s *cf_capt_family_ops(bool series6000);
