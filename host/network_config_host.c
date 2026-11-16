#include "network_config.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 network_config_t c={0};
 assert(network_config_parse(&c,"192.168.10.36","255.255.255.0","192.168.10.1","1.1.1.1")&&c.enabled);
 assert(network_config_parse(&c,"10.10.2.4","255.255.0.0","10.10.0.1","8.8.8.8"));
 const char *ips[]={"192.168.10.0","192.168.10.255","127.0.0.1","0.0.0.0","224.0.0.1","192.168.10.1","1.2.3","192.168.10.999","<script>"};
 for(unsigned i=0;i<sizeof(ips)/sizeof(*ips);i++)assert(!network_config_parse(&c,ips[i],"255.255.255.0","192.168.10.1","1.1.1.1"));
 assert(!network_config_parse(&c,"192.168.10.36","255.0.255.0","192.168.10.1","1.1.1.1"));
 assert(!network_config_parse(&c,"192.168.10.36","255.255.255.255","192.168.10.1","1.1.1.1"));
 assert(!network_config_parse(&c,"192.168.10.36","255.255.255.0","192.168.20.1","1.1.1.1"));
 assert(!network_config_parse(&c,"192.168.10.36","255.255.255.0","192.168.10.1",""));
 assert(!network_config_parse(&c,NULL,"255.255.255.0","192.168.10.1","1.1.1.1"));
 puts("Static IPv4: valid subnets, bad masks, multicast/loopback/network/broadcast, gateway and DNS validation passed");
}
