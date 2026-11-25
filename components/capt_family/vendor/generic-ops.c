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

#include "cf_generic-ops.h"

#include "cf_std.h"
#include "cf_hiscoa-common.h"
#include "cf_hiscoa-compress.h"
#include "cf_capt-command.h"
#include "cf_capt-status.h"
#include "cf_printer.h"

#include <stdlib.h>

size_t cf_ops_compress_band_hiscoa(struct printer_state_s *state,
		void *band, size_t size,
		const void *pixels, unsigned line_size, unsigned num_lines, bool last)
{
	(void) state;
	return cf_hiscoa_compress_band(band, size, pixels, line_size, num_lines,
					last ? HISCOA_EOB_LAST : HISCOA_EOB_NORMAL, &cf_hiscoa_default_params);
}

void cf_ops_send_band_hiscoa(struct printer_state_s *state, const void *data, size_t size)
{
	const uint8_t *pdata = (const uint8_t *) data;
	while (size) {
		size_t send = 0xFF00;
		if (send > size)
			send = size;
		state->isend += 1;
		if (state->isend % 16 == 0)
			cf_capt_wait_xready_only();
		cf_capt_send(CAPT_PRINT_DATA, pdata, send);
		pdata += send;
		size -= send;
	}
}
