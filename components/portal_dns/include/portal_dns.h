#pragma once
#include <stddef.h>
#include <stdint.h>
// Strict, bounded DNS A responder; no recursion or compression pointer chasing.
size_t portal_dns_reply(const uint8_t *query, size_t length, uint8_t *out,
                        size_t capacity, const uint8_t ip[4]);
