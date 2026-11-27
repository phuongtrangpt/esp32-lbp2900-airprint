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

#pragma once

#include "cf_std.h"
#include "cf_hiscoa-common.h"

size_t cf_hiscoa_compress_band(void *buf, size_t size,
	const void *band, unsigned line_size, unsigned nlines,
	enum cf_hiscoa_eob_type eob_type,
	const struct cf_hiscoa_params *params);

size_t cf_hiscoa_format_params(void *buf, size_t size,
	const struct cf_hiscoa_params *params);
