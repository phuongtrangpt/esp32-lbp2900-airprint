// SPDX-License-Identifier: GPL-3.0-or-later
#include "hp_offload.h"
#include "../../src/hp_raw_xqx.h"
#include <string.h>
#include <errno.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
static const uint8_t magic[8]={'V','P','H','P','O','0','1','\n'};
static const uint8_t ack_magic[8]={'V','P','H','P','A','0','1','\n'};
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p)<<32|get32(p+4); }
static void put32(uint8_t *p,uint32_t n) { p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n; }
static int exact(int fd,uint8_t *p,size_t n,bool sending) {
    while(n) {
        int got=sending?send(fd,p,n,MSG_NOSIGNAL):recv(fd,p,n,0);
        if(got<0 && errno==EINTR)continue;
        if(got<=0 || (size_t)got>n)return -1;
        p+=got;n-=got;
    }
    return 0;
}
static int reply(int fd,uint32_t status,uint32_t sequence,uint64_t accepted) {
    uint8_t b[24];memcpy(b,ack_magic,8);put32(b+8,status);put32(b+12,sequence);
    put32(b+16,accepted>>32);put32(b+20,accepted);return exact(fd,b,sizeof(b),true);
}
uint32_t hp_offload_crc(uint32_t state,const uint8_t *p,size_t n) {
    while(n--) {state^=*p++;for(int i=0;i<8;i++)state=(state>>1)^((0u-(state&1u))&0xedb88320u);}
    return state;
}
int hp_offload_session(int fd,const hp_offload_io_t *io,hp_offload_state_t *state,uint8_t buffer[HP_OFFLOAD_CHUNK]) {
    struct timeval timeout={.tv_sec=30};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    uint8_t header[32];int status=HP_PC_INVALID;
    if(exact(fd,header,sizeof(header),false)<0)return HP_PC_TRUNCATED;
    if(memcmp(header,magic,8)) {reply(fd,status,0,0);return status;}
    uint32_t op=get32(header+8),expected_crc=get32(header+20);
    uint64_t total=get64(header+12),id=get64(header+24);
    if(op==3 && !total && !expected_crc && id) {
        status=state->job_id==id?state->status:HP_PC_NOT_FOUND;
        reply(fd,status,0,state->job_id==id?state->accepted:0);return status;
    }
    if(op==1 && !total && !expected_crc && !id) {
        status=io->ready(io->ctx);reply(fd,status,0,0);return status;
    }
    if(op!=2 || total<128 || total>HP_OFFLOAD_MAX_BYTES || !id) {reply(fd,status,0,0);return status;}
    if(state->job_id==id) {
        status=(state->total==total && state->crc==expected_crc)?state->status:HP_PC_INVALID;
        reply(fd,status,0,state->accepted);return status;
    }
    status=io->ready(io->ctx);
    if(!status)status=io->begin(io->ctx);
    if(status) {reply(fd,status,0,0);return status;}
    *state=(hp_offload_state_t){.job_id=id,.total=total,.crc=expected_crc,.status=HP_PC_TRUNCATED};
    bool output_started=false;uint32_t sequence=0,crc=0xffffffffu;
    hp_raw_xqx_t parser={0};
    if(reply(fd,HP_PC_READY,0,0)<0) {status=HP_PC_TRUNCATED;goto fail;}
    for(;;) {
        uint8_t frame[12];status=HP_PC_TRUNCATED;
        if(exact(fd,frame,sizeof(frame),false)<0)goto fail;
        uint32_t seq=get32(frame),size=get32(frame+4),sum=get32(frame+8);
        status=HP_PC_INVALID;
        if(seq!=sequence || size>HP_OFFLOAD_CHUNK || size>total-state->accepted)goto fail;
        if(!size) {
            if(sum || state->accepted!=total)goto fail;
            status=HP_PC_CRC;
            if((crc^0xffffffffu)!=expected_crc)goto fail;
            status=HP_PC_TRUNCATED;
            if(!hp_raw_xqx_complete(&parser) || parser.trailer_bytes!=sizeof(hp_raw_xqx_trailer)-1)goto fail;
            // USB delivery only. This is NOT confirmation that paper has exited.
            io->end(io->ctx,true);state->status=HP_PC_DONE;
            reply(fd,HP_PC_DONE,sequence,state->accepted);return HP_PC_DONE;
        }
        uint64_t remaining=total-state->accepted;
        if(size!=(remaining>HP_OFFLOAD_CHUNK?HP_OFFLOAD_CHUNK:remaining))goto fail;
        status=HP_PC_TRUNCATED;
        if(exact(fd,buffer,size,false)<0)goto fail;
        status=HP_PC_CRC;
        if((hp_offload_crc(0xffffffffu,buffer,size)^0xffffffffu)!=sum)goto fail;
        status=HP_PC_INVALID;
        if(!sequence && (size<17 || memcmp(buffer,"\033%-12345X@PJL JOB",17)))goto fail;
        hp_raw_xqx_feed(&parser,buffer,size);
        if(parser.state==XQX_UNKNOWN || (!sequence && parser.state==XQX_PREAMBLE))goto fail;
        crc=hp_offload_crc(crc,buffer,size);
        output_started=true;status=HP_PC_USB;
        if(io->write(io->ctx,buffer,size)<0 || io->flush(io->ctx)<0 || (io->poll && io->poll(io->ctx)<0))goto fail;
        state->accepted+=size;
        if(reply(fd,HP_PC_ACK,sequence,state->accepted)<0) {status=HP_PC_TRUNCATED;goto fail;}
        sequence++;
    }
fail:
    io->end(io->ctx,!output_started);
    state->status=output_started?HP_PC_UNKNOWN:(uint32_t)status;
    reply(fd,status,sequence,state->accepted);
    return status;
}
