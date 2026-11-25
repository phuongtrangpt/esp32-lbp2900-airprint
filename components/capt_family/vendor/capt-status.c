/*
 * Copyright (C) 2013 Alexey Galakhov <agalakhov@gmail.com>
 *
 * Licensed under the GNU General Public License Version 3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "cf_capt-status.h"

#include "cf_std.h"
#include "cf_capt_transport.h"
#include "cf_word.h"
#include "cf_capt-command.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static _Thread_local struct cf_capt_status_s status;

static inline char bit(enum cf_capt_flags flag)
{
	return FLAG(&status, flag) ? '1' : '0';
}

static void print_status(void)
{
	cf_capt_log("DEBUG: CAPT: printer status P1=%c P2=%c B=%c B0=%c B1=%c nE=%c\n",
		bit(CAPT_FL_NOPAPER1), bit(CAPT_FL_NOPAPER2),
		bit(CAPT_FL_BUTTON_ON),
		bit(CAPT_FL_BUTTON), bit(CAPT_FL_BUTTON1),
		bit(CAPT_FL_nERROR)
	);
	cf_capt_log("DEBUG: CAPT: pages %u/%u/%u/%u\n",
		status.page_decoding,
		status.page_printing,
		status.page_out,
		status.page_completed
	);
}

static void decode_status(const uint8_t *s, size_t size)
{
    // Extended counters are mandatory for this family. Partial replies must
    // never reuse stale counters or read outside the received payload.
    if (size != 2 && size != 10 && size < 40)
        cf_capt_fail("CAPT short status reply");
    memset(&status, 0, sizeof(status));
    status.status[0] = WORD(s[0], s[1]);
    if (size == 2) return;
    status.status[1] = WORD(s[8], s[9]);
    if (size == 10) return;
    status.status[2] = WORD(s[10], s[11]);
    status.status[3] = WORD(s[12], s[13]);
    status.page_decoding = WORD(s[14], s[15]);
    status.page_printing = WORD(s[16], s[17]);
    status.page_out = WORD(s[18], s[19]);
    status.page_completed = WORD(s[20], s[21]);
    status.page_received = WORD(s[34], s[35]);
    status.status[4] = WORD(s[24], s[25]);
    status.status[5] = WORD(s[30], s[31]);
    status.status[6] = WORD(s[38], s[39]);
}

static void download_status(uint16_t cmd)
{
	uint8_t buf[256];
	size_t size = sizeof(buf);
	cf_capt_sendrecv(cmd, NULL, 0, buf, &size);
	if (cmd == CAPT_CHKXSTATUS && size < 40) cf_capt_fail("CAPT extended status missing counters");
	decode_status(buf, size);
}

void cf_capt_init_status(void)
{
	memset(&status, 0, sizeof(status));
}

const struct cf_capt_status_s *cf_capt_get_status(void)
{
	download_status(CAPT_CHKSTATUS);
	return &status;
}

const struct cf_capt_status_s *cf_capt_get_xstatus_only(void)
{
	download_status(CAPT_CHKXSTATUS);
	if (debug) print_status();
	/*
	if (FLAG(&status, CAPT_FL_JOBSTAT_CHNG)) {
	   cf_capt_sendrecv(CAPT_CHKJOBSTAT, NULL, 0, NULL, 0);
	   if (debug) print_status();
	}
	*/

	return &status;
}

const struct cf_capt_status_s *cf_capt_get_xstatus(void)
{
	download_status(CAPT_CHKSTATUS);
	if (FLAG(&status, CAPT_FL_XSTATUS_CHNG))
		cf_capt_get_xstatus_only();
	return &status;
}

void cf_capt_wait_ready(void)
{
	while (FLAG(cf_capt_get_status(), CAPT_FL_BUSY))
		cf_capt_pause(1000);
}

void cf_capt_wait_xready(void)
{
	while (FLAG(cf_capt_get_xstatus(), CAPT_FL_BUSY))
		cf_capt_pause(1000);
}

void cf_capt_wait_xready_only(void)
{
       while (FLAG(cf_capt_get_xstatus_only(), CAPT_FL_BUSY))
               cf_capt_pause(1000);
}
