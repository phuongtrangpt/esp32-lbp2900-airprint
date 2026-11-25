// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define HP_OFFLOAD_PORT 9101
#define HP_OFFLOAD_CHUNK 4096
#define HP_OFFLOAD_MAX_BYTES (256u*1024u*1024u)
enum { HP_PC_READY=0, HP_PC_ACK=1, HP_PC_DONE=2, HP_PC_INVALID=100,
       HP_PC_BUSY=101, HP_PC_MODEL=102, HP_PC_USB=103, HP_PC_CRC=104,
       HP_PC_TRUNCATED=105, HP_PC_UNKNOWN=108, HP_PC_NOT_FOUND=109 };
typedef struct {
    void *ctx;
    int (*ready)(void *ctx); // zero or HP_PC_* error; never claims USB
    int (*begin)(void *ctx);
    int (*write)(void *ctx,const uint8_t *data,size_t size);
    int (*flush)(void *ctx);
    int (*poll)(void *ctx); // nonblocking USB back-channel drain
    void (*end)(void *ctx,bool link_ok);
} hp_offload_io_t;
typedef struct {
    uint64_t job_id,total,accepted;
    uint32_t crc,status;
} hp_offload_state_t;
uint32_t hp_offload_crc(uint32_t state,const uint8_t *data,size_t size);
// One connection/transaction. All buffers are bounded; caller serializes sessions.
int hp_offload_session(int fd,const hp_offload_io_t *io,hp_offload_state_t *state,uint8_t buffer[HP_OFFLOAD_CHUNK]);
