#pragma once
#include <stdint.h>
#include <stddef.h>
// Decode bounded upstream tar.gz and wrap exactly sihpP1006.img as HPDL.
int hp_bootstrap_build(const uint8_t *archive,size_t n,uint8_t **dl,size_t *len);
uint32_t hp_bootstrap_crc32(const uint8_t *p,size_t n);
