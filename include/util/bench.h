#ifndef KOLIBRI_UTIL_BENCH_H
#define KOLIBRI_UTIL_BENCH_H

#include <stddef.h>

#include "util/config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t iterations;
    size_t warmup;
    const char *output_path;
    int include_profile;
} bench_options_t;

void bench_options_init(bench_options_t *opts);
int bench_run_all(const kolibri_config_t *cfg, const bench_options_t *opts);

#ifdef __cplusplus
}
#endif

#endif
