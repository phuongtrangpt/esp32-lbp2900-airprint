/*
 * Copyright (C) 2013 Alexey Galakhov <agalakhov@gmail.com>
 * Copyright (C) 2016 Alexei Gordeev <KP1533TM2@gmail.com>
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

#include "cf_std.h"
#include "cf_capt_transport.h"
#include "cf_word.h"
#include "cf_capt-command.h"
#include "cf_capt-status.h"
#include "cf_generic-ops.h"
#include "cf_hiscoa-common.h"
#include "cf_hiscoa-compress.h"
#include "cf_paper.h"
#include "cf_printer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static _Thread_local uint16_t cf_job;

struct printer_gpio_s {
	const uint8_t (*init);
	const uint8_t (*blink);
};

struct lbp2900_ops_s {
	struct printer_ops_s ops;
	struct printer_gpio_s gpio;

	const struct cf_capt_status_s * (*get_status) (void);
	void (*wait_ready) (void);
};

static const uint8_t magicbuf_0[] = {
	0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t magicbuf_2[] = {
	0xEE, 0xDB, 0xEA, 0xAD, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};




static const uint8_t lbp3010_gpio_blink[] = {
        /* led */ 0x31, 0x00, 0x00, /* S6 */ 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, /* S7 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t lbp3010_gpio_init[] = {
        /* led */ 0x13, 0x00, 0x00, /* S6 */ 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, /* S7 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t lbp6000_job_init[] = {
        0x01, 0x00,
};

static const struct cf_capt_status_s *lbp2900_get_status(const struct printer_ops_s *ops)
{
	const struct lbp2900_ops_s *lops = container_of(ops, struct lbp2900_ops_s, ops);
	return lops->get_status();
}

static void lbp2900_wait_ready(const struct printer_ops_s *ops)
{
	const struct lbp2900_ops_s *lops = container_of(ops, struct lbp2900_ops_s, ops);
	lops->wait_ready();
}

static void send_job_start(uint8_t fg, uint16_t page)
{
	uint8_t ml = 0x00; /* host name lenght */
	uint8_t ul = 0x00; /* user name lenght */
	uint8_t nl = 0x00; /* document name lenght */
	time_t rawtime = time(NULL);
	struct tm tm_storage;
	const struct tm *tm = localtime_r(&rawtime, &tm_storage);
	if (!tm) cf_capt_fail("CAPT date conversion failed");
	uint8_t buf[32 + 40 + ml + ul + nl];
	uint8_t head[32] = {
		0x00, 0x00, 0x00, 0x00, LO(page), HI(page), 0x00, 0x00,
		ml, 0x00, ul, 0x00, nl, 0x00, 0x00, 0x00,
		fg, 0x01, LO(cf_job), HI(cf_job),
		/*-60 */ 0xC4, 0xFF,
		/*-120*/ 0x88, 0xFF,
		LO(tm->tm_year), HI(tm->tm_year), (uint8_t) tm->tm_mon, (uint8_t) tm->tm_mday,
		(uint8_t) tm->tm_hour, (uint8_t) tm->tm_min, (uint8_t) tm->tm_sec,
		0x01,
	};
	memcpy(buf, head, sizeof(head));
	memset(buf + 32, 0, 40 + ml + ul + nl);
	cf_capt_sendrecv(CAPT_JOB_SETUP, buf, sizeof(buf), NULL, 0);
}



static void lbp3010_job_prologue(struct printer_state_s *state)
{
	(void) state;
	uint8_t buf[8];
	size_t size = sizeof(buf);

	cf_capt_sendrecv(CAPT_IDENT, NULL, 0, NULL, 0);
	cf_capt_pause(1000);
	cf_capt_init_status();
	lbp2900_get_status(state->ops);

	cf_capt_sendrecv(CAPT_START_0, NULL, 0, NULL, 0);
	cf_capt_sendrecv(CAPT_JOB_BEGIN, magicbuf_0, ARRAY_SIZE(magicbuf_0), buf, &size);
	if (size < 4) {
		cf_capt_restart_required(true);
		cf_capt_fail("CAPT reservation reply missing job ID");
	}
	if (WORD(buf[0], buf[1])) cf_capt_fail("CAPT reservation rejected");
	state->reserved = true;
	cf_job=WORD(buf[2], buf[3]);

	cf_capt_sendrecv(CAPT_GPIO, lbp3010_gpio_init, ARRAY_SIZE(lbp3010_gpio_init), NULL, 0);
	lbp2900_wait_ready(state->ops);

	send_job_start(1, 0);
	lbp2900_wait_ready(state->ops);
}

static void lbp6000_job_prologue(struct printer_state_s *state)
{
	(void) state;
	uint8_t buf[8];
	size_t size = sizeof(buf);

	cf_capt_sendrecv(CAPT_IDENT, NULL, 0, NULL, 0);
	cf_capt_pause(1000);
	cf_capt_init_status();
	lbp2900_get_status(state->ops);

	cf_capt_sendrecv(CAPT_START_0, NULL, 0, NULL, 0);
	cf_capt_sendrecv(CAPT_JOB_BEGIN, magicbuf_0, ARRAY_SIZE(magicbuf_0), buf, &size);
	if (size < 4) {
		cf_capt_restart_required(true);
		cf_capt_fail("CAPT reservation reply missing job ID");
	}
	if (WORD(buf[0], buf[1])) cf_capt_fail("CAPT reservation rejected");
	state->reserved = true;
	cf_job=WORD(buf[2], buf[3]);

	cf_capt_sendrecv(CAPT_GPIO, lbp3010_gpio_init, ARRAY_SIZE(lbp3010_gpio_init), NULL, 0);
	lbp2900_wait_ready(state->ops);

	cf_capt_sendrecv(CAPT_LBP6000_SETUP_0, lbp6000_job_init, ARRAY_SIZE(lbp6000_job_init), NULL, 0);
	lbp2900_wait_ready(state->ops);

	send_job_start(1, 0);
	lbp2900_wait_ready(state->ops);
}

static bool lbp2900_page_prologue(struct printer_state_s *state, const struct page_dims_s *dims)
{
	const struct cf_capt_status_s *status;
	size_t s;
	uint8_t buf[16];

	uint8_t sz = 0x00; /* page size */
	uint8_t save = dims->toner_save;
	uint8_t ink_k = (dims->ink_k<<2);
	uint8_t fm = 0x00; /* fuser mode (temperature?) */
	uint8_t air = 0x02; /* automatic image refinement */

	switch (dims->media_type) {
		case 0x00:
		case 0x02:
			/* Plain Paper & Plain Paper L */
			fm = 0x01;
			break;
		case 0x01:
			/* Thick Paper */
			fm = 0x01;
			break;
		case 0x03:
			/* Thick Paper H */
			fm = 0x02;
			break;
		case 0x04:
			/* Transparency */
			fm = 0x13;
			break;
		case 0x05:
			/* Transparency */
			fm = 0x14;
			break;
		case 0x06:
			/* Envelope */
			fm = 0x1C;
			break;
		default:
			fm = 0x01;
	}
	cf_capt_log("DEBUG: CAPT: media_type=%u, fm=%u\n", dims->media_type, fm);

	if ( strncmp(dims->media_size, "A4", 2) == 0 ) sz = 0x02;
	else if ( strncmp(dims->media_size, "A5", 2) == 0 ) sz = 0x03;
	else if ( strncmp(dims->media_size, "B5", 2) == 0 ) sz = 0x07;
	else if ( strncmp(dims->media_size, "Executive", 9) == 0 ) sz = 0x0A;
	else if ( strncmp(dims->media_size, "Legal", 5) == 0 ) sz = 0x0C;
	else if ( strncmp(dims->media_size, "Letter", 6) ==0 ) sz = 0x0D;
	else if ( strncmp(dims->media_size, "EnvC5", 5) ==0 ) sz = 0x15;
	else if ( strncmp(dims->media_size, "Env10", 5) == 0 ) sz = 0x16;
	else if ( strncmp(dims->media_size, "EnvMonarch", 10) == 0 ) sz = 0x17;
	else if ( strncmp(dims->media_size, "EnvDL", 5) == 0 ) sz = 0x18;
	else if ( strncmp(dims->media_size, "3x5", 3) ==0 ) sz = 0x40;
	else if ( strncmp(dims->media_size, "PRC16K", 6) ==0 ) sz = 0xD4;
	else sz = 0x02;
	cf_capt_log("DEBUG: CAPT: media_size=%s, fm=%u\n", dims->media_size, sz);

	uint8_t pageparms[] = {
		/* Bytes 0-21 (0x00 to 0x15) */
		0x00, 0x00, 0x30, 0x2A, sz, 0x00, 0x00, 0x00,
		ink_k, 0x1C, 0x1C, 0x1C, dims->media_type, dims->media_adapt, 0x04, 0x00,
		0x01, 0x01, air, save, 0x00, 0x00,
		/* Bytes 22-33 (0x16 to 0x21) */
		LO(dims->margin_height), HI(dims->margin_height),
		LO(dims->margin_width), HI(dims->margin_width),
		LO(dims->line_size), HI(dims->line_size),
		LO(dims->num_lines), HI(dims->num_lines),
		LO(dims->paper_width), HI(dims->paper_width),
		LO(dims->paper_height), HI(dims->paper_height),
		/* Bytes 34-39 (0x22 to 0x27) */
		0x00, 0x00, fm, 0x00, 0x00, 0x00,
		/* Spare bytes for later
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		*/
	};

	(void) state;

	status = lbp2900_get_status(state->ops);
	if (FLAG(status, CAPT_FL_UNINIT1) || FLAG(status, CAPT_FL_UNINIT2)) {
		cf_capt_sendrecv(CAPT_START_1, NULL, 0, NULL, 0);
		cf_capt_sendrecv(CAPT_START_2, NULL, 0, NULL, 0);
		cf_capt_sendrecv(CAPT_START_3, NULL, 0, NULL, 0);
		//lbp2900_get_status(state->ops);
		lbp2900_wait_ready(state->ops);

		cf_capt_sendrecv(CAPT_UPLOAD_2, magicbuf_2, ARRAY_SIZE(magicbuf_2), NULL, 0);
		lbp2900_wait_ready(state->ops);
	}

	while (1) {
		if (! FLAG(lbp2900_get_status(state->ops), CAPT_FL_BUFFERFULL))
			break;
		cf_capt_pause(1000);
	}

	cf_capt_multi_begin(CAPT_SET_PARMS);
	cf_capt_multi_add(CAPT_SET_PARM_PAGE, pageparms, sizeof(pageparms));
	s = cf_hiscoa_format_params(buf, sizeof(buf), &cf_hiscoa_default_params);
	cf_capt_multi_add(CAPT_SET_PARM_HISCOA, buf, s);
	cf_capt_multi_add(CAPT_SET_PARM_1, NULL, 0);
	cf_capt_multi_add(CAPT_SET_PARM_2, NULL, 0);
	cf_capt_multi_send();

	return true;
}

static bool lbp2900_page_epilogue(struct printer_state_s *state, const struct page_dims_s *dims)
{
	(void) dims;
	const struct cf_capt_status_s *status;

	cf_capt_send(CAPT_PRINT_DATA_END, NULL, 0);

	/* waiting until the page is received */
	while (1) {
	  cf_capt_pause(1000);
	  status = lbp2900_get_status(state->ops);
	  if (status->page_received == state->ipage && status->page_decoding == state->ipage)
	    break;
	}
	send_job_start(2, status->page_decoding);
	lbp2900_wait_ready(state->ops);

	uint8_t buf[2] = { LO(status->page_decoding), HI(status->page_decoding) };
	cf_capt_sendrecv(CAPT_FIRE, buf, 2, NULL, 0);
	lbp2900_wait_ready(state->ops);

	send_job_start(6, status->page_decoding);

	while (1) {
		const struct cf_capt_status_s *status = lbp2900_get_status(state->ops);
		/* Interesting. Using page_printing here results in shifted print */
		if (status->page_out == state->ipage && status->page_decoding == state->ipage)
			return true;
		if (FLAG(status, CAPT_FL_NOPAPER2) || FLAG(status, CAPT_FL_NOPAPER1)) {
			cf_capt_log("DEBUG: CAPT: no paper\n");
			if (FLAG(status, CAPT_FL_PRINTING) || FLAG(status, CAPT_FL_PROCESSING1)) {
				cf_capt_pause(1000);
				continue;
			}
			return false;
		}
		cf_capt_pause(1000);
	}
}

static void lbp2900_job_epilogue(struct printer_state_s *state)
{
	uint8_t jbuf[2] = { LO(cf_job), HI(cf_job) };

	while (1) {
		const struct cf_capt_status_s *status = lbp2900_get_status(state->ops);
		if (status->page_completed == state->ipage && status->page_decoding == state->ipage) {
			send_job_start(4, status->page_completed);
			break;
		}
		cf_capt_pause(1000);
	}
	size_t reply_size = 2;
	uint8_t reply[2];
	cf_capt_sendrecv(CAPT_JOB_END, jbuf, 2, reply, &reply_size);
	if (reply_size != 2 || WORD(reply[0], reply[1])) cf_capt_fail("CAPT release rejected");
	state->reserved = false;
}

static void lbp2900_page_setup(struct printer_state_s *state,
		struct page_dims_s *dims,
		unsigned width, unsigned height)
{
	/* FIXME: Do we still need this function? */
	(void) state;
	(void) width;
	(void) height;
	(void) dims;
	/* Get raster dimensions straight from CUPS in paper.c */
	//dims->num_lines = dims->paper_height;
	//dims->line_size = dims->paper_width / 8;
	//dims->band_size = 70;
}


static void lbp3010_cancel_cleanup(struct printer_state_s *state)
{
	(void) state;
	(void) state;
	const struct cf_capt_status_s *status = lbp2900_get_status(state->ops);
	uint8_t jbuf[2] = { LO(cf_job), HI(cf_job) };

	cf_capt_sendrecv(CAPT_GPIO, lbp3010_gpio_init, ARRAY_SIZE(lbp3010_gpio_init), NULL, 0);
	send_job_start(4, status->page_completed);
	size_t reply_size = 2;
	uint8_t reply[2];
	cf_capt_sendrecv(CAPT_JOB_END, jbuf, 2, reply, &reply_size);
	if (reply_size != 2 || WORD(reply[0], reply[1])) cf_capt_fail("CAPT release rejected");
	state->reserved = false;
}


static void lbp3010_wait_user(struct printer_state_s *state)
{
	(void) state;

	cf_capt_sendrecv(CAPT_GPIO, lbp3010_gpio_blink, ARRAY_SIZE(lbp3010_gpio_blink), NULL, 0);
	lbp2900_wait_ready(state->ops);

	while (1) {
		const struct cf_capt_status_s *status = lbp2900_get_status(state->ops);
		if (FLAG(status, CAPT_FL_BUTTON_ON)) {
			cf_capt_log("DEBUG: CAPT: button activated\n");
		}
		if (FLAG(status, CAPT_FL_nERROR)) {
			cf_capt_log("DEBUG: CAPT: (virtual) button pressed\n");
			break;
		}
		cf_capt_pause(1000);
	}

	cf_capt_sendrecv(CAPT_GPIO, lbp3010_gpio_init, ARRAY_SIZE(lbp3010_gpio_init), NULL, 0);
	lbp2900_wait_ready(state->ops);
}



static struct lbp2900_ops_s lbp3010_ops = {
	.ops = {
		.job_prologue = lbp3010_job_prologue,
		.job_epilogue = lbp2900_job_epilogue,
		.page_setup = lbp2900_page_setup,
		.page_prologue = lbp2900_page_prologue,
		.page_epilogue = lbp2900_page_epilogue,
		.compress_band = cf_ops_compress_band_hiscoa,
		.send_band = cf_ops_send_band_hiscoa,
		.cancel_cleanup = lbp3010_cancel_cleanup,
		.wait_user = lbp3010_wait_user,
	},
	.gpio = {
		.init = lbp3010_gpio_init,
		.blink = lbp3010_gpio_blink,
	},
	.get_status = cf_capt_get_xstatus_only,
	.wait_ready = cf_capt_wait_xready_only,
};

static struct lbp2900_ops_s lbp6000_ops = {
	.ops = {
		.job_prologue = lbp6000_job_prologue,
		.job_epilogue = lbp2900_job_epilogue,
		.page_setup = lbp2900_page_setup,
		.page_prologue = lbp2900_page_prologue,
		.page_epilogue = lbp2900_page_epilogue,
		.compress_band = cf_ops_compress_band_hiscoa,
		.send_band = cf_ops_send_band_hiscoa,
		.cancel_cleanup = lbp3010_cancel_cleanup,
		.wait_user = lbp3010_wait_user,
	},
	.gpio = {
		.init = lbp3010_gpio_init,
		.blink = lbp3010_gpio_blink,
	},
	.get_status = cf_capt_get_xstatus_only,
	.wait_ready = cf_capt_wait_xready_only,
};


const struct printer_ops_s *cf_capt_family_ops(bool series6000)
{ return series6000 ? &lbp6000_ops.ops : &lbp3010_ops.ops; }
