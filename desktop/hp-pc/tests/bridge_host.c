// SPDX-License-Identifier: GPL-3.0-or-later
// Real protocol implementation, simulated USB sink only.
#include "hp_offload.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
typedef struct { FILE *out;int mode;int stalled; } sink;
static int ready(void *ctx){int mode=((sink*)ctx)->mode;return mode==1?HP_PC_MODEL:mode==2?HP_PC_BUSY:0;}
static int begin(void *ctx){(void)ctx;return 0;}
static int write_usb(void *ctx,const uint8_t*p,size_t n){sink*s=ctx;return s->mode==3?-1:fwrite(p,1,n,s->out)==n?0:-1;}
static int flush(void *ctx){sink*s=ctx;if(s->mode==4 && !s->stalled){s->stalled=1;sleep(13);}return fflush(s->out);}
static void end(void *ctx,bool ok){(void)ok;fflush(((sink*)ctx)->out);}
int main(int argc,char **argv){
    if(argc<2)return 2;sink s={.out=fopen(argv[1],"wb"),.mode=argc>2?atoi(argv[2]):0};if(!s.out)return 2;
    int fd=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    if(bind(fd,(struct sockaddr*)&a,sizeof(a))||listen(fd,4))return 2;
    socklen_t len=sizeof(a);getsockname(fd,(struct sockaddr*)&a,&len);printf("%u\n",ntohs(a.sin_port));fflush(stdout);
    hp_offload_io_t io={.ctx=&s,.ready=ready,.begin=begin,.write=write_usb,.flush=flush,.end=end};
    hp_offload_state_t state={0};uint8_t buf[HP_OFFLOAD_CHUNK];
    for(;;){int client=accept(fd,NULL,NULL);if(client<0)break;hp_offload_session(client,&io,&state,buf);close(client);}
    fclose(s.out);close(fd);return 0;
}
