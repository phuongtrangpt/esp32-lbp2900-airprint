#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct { uint32_t ip,mask,gateway,dns; bool enabled; } network_config_t;
bool network_config_parse(network_config_t *out,const char *ip,const char *mask,const char *gateway,const char *dns);
