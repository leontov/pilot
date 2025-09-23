#ifndef KOLIBRI_UTIL_CONFIG_H
#define KOLIBRI_UTIL_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char host[64];
    uint16_t port;
} http_config_t;

typedef struct {
    uint32_t max_steps;
    uint32_t max_stack;
    uint32_t trace_depth;
} vm_config_t;

typedef struct {
    http_config_t http;
    vm_config_t vm;
    uint32_t seed;
} kolibri_config_t;

int config_load(const char *path, kolibri_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif
