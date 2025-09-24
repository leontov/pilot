#ifndef KOLIBRI_UTIL_BENCH_H
#define KOLIBRI_UTIL_BENCH_H

#include <stddef.h>

#include "util/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BENCH_MAX_OVERRIDES 8

typedef struct {
    char name[32];
    int has_p95;
    double p95_ms;
    int has_p99;
    double p99_ms;
} bench_threshold_override_t;

typedef struct {
    size_t iterations;
    size_t warmup;
    const char *output_path;
    int include_profile;
    bench_threshold_override_t overrides[BENCH_MAX_OVERRIDES];
    size_t override_count;
} bench_options_t;

void bench_options_init(bench_options_t *opts);
int bench_options_add_threshold_override(bench_options_t *opts,
                                         const char *name,
                                         int has_p95,
                                         double p95_ms,
                                         int has_p99,
                                         double p99_ms);
int bench_run_all(const kolibri_config_t *cfg, const bench_options_t *opts);

#ifdef __cplusplus
}
#endif

#endif
