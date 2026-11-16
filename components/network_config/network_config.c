#include "network_config.h"
#include <arpa/inet.h>
#include <string.h>
static bool host(uint32_t n) { n=ntohl(n); return (n>>24)>0 && (n>>24)!=127 && (n>>24)<224; }
bool network_config_parse(network_config_t *out,const char *ip,const char *mask,const char *gateway,const char *dns) {
    network_config_t c={0};
    if(!out || !ip || !mask || !gateway || !dns || inet_pton(AF_INET,ip,&c.ip)!=1 ||
       inet_pton(AF_INET,mask,&c.mask)!=1 || inet_pton(AF_INET,gateway,&c.gateway)!=1 ||
       inet_pton(AF_INET,dns,&c.dns)!=1) return false;
    uint32_t m=ntohl(c.mask),inverse=~m;
    if(!m || inverse<3 || (inverse&(inverse+1))) return false;
    if(!host(c.ip) || !host(c.gateway) || !host(c.dns) || c.ip==c.gateway ||
       (c.ip&c.mask)!=(c.gateway&c.mask) || !(c.ip&~c.mask) || (c.ip&~c.mask)==~c.mask ||
       !(c.gateway&~c.mask) || (c.gateway&~c.mask)==~c.mask) return false;
    c.enabled=true; *out=c; return true;
}
