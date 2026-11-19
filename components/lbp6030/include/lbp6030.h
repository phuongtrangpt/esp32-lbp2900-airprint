// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>
// Four-bit 600x400 raster. Returns encoded size, or zero on invalid input/capacity.
size_t lbp6030_encode(const uint8_t *raw, size_t stride, size_t rows, int last,
                      uint8_t *out, size_t capacity);
