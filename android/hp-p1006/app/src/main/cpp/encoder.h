// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct vp_encoder vp_encoder;
vp_encoder *vp_begin(const char *path,int paper);
int vp_page(vp_encoder *ctx,const uint8_t *rgba,int width,int height,size_t stride);
int vp_end(vp_encoder *ctx,int success);
