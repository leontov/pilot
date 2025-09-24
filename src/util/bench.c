#define _POSIX_C_SOURCE 200809L
#include "util/bench.h"

#include "fkv/fkv.h"
#include "http/http_routes.h"
#include "util/log.h"
#include "vm/vm.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

typedef struct {
    const char *name;
    double avg_ms;
    double p95_ms;
    double p99_ms;
    size_t samples;
    double threshold_p95_ms;
    double threshold_p99_ms;
    int status; /* 0 = ok, 1 = regression, -1 = error */
    double *profile_ms;
} bench_result_t;

static const bench_threshold_override_t *find_override(const bench_options_t *opts, const char *name) {
    if (!opts || !name) {
        return NULL;
    }
    for (size_t i = 0; i < opts->override_count; ++i) {
        if (strcmp(opts->overrides[i].name, name) == 0) {
            return &opts->overrides[i];
        }
    }
    return NULL;
}

static void apply_threshold_override(const bench_options_t *opts, bench_result_t *result) {
    if (!result) {
        return;
    }
    const bench_threshold_override_t *ov = find_override(opts, result->name);
    if (!ov) {
        return;
    }
    if (ov->has_p95) {
        result->threshold_p95_ms = ov->p95_ms;
    }
    if (ov->has_p99) {
        result->threshold_p99_ms = ov->p99_ms;
    }
}

typedef struct {
    vm_limits_t limits;
    prog_t program;
} bench_vm_ctx_t;

typedef struct {
    size_t value_len;
    size_t key_len;
    size_t key_count;
    uint8_t prefix[4];
    size_t prefix_len;
    size_t limit;
} bench_fkv_ctx_t;

typedef struct {
    kolibri_config_t cfg;
    const char *method;
    const char *path;
    const char *body;
} bench_http_ctx_t;

static double timespec_to_ms_diff(const struct timespec *start, const struct timespec *end) {
    double s = (double)(end->tv_sec - start->tv_sec) * 1000.0;
    long ns = end->tv_nsec - start->tv_nsec;
    return s + (double)ns / 1e6;
}

static int ensure_parent_dir(const char *path) {
    if (!path) {
        return 0;
    }
    const char *slash = strrchr(path, '/');
    if (!slash) {
        return 0;
    }
    size_t len = (size_t)(slash - path);
    if (len == 0) {
        return 0;
    }
    char *dir = calloc(len + 1, 1);
    if (!dir) {
        return -1;
    }
    memcpy(dir, path, len);
    struct stat st;
    if (stat(dir, &st) == 0) {
        free(dir);
        return 0;
    }
    if (errno != ENOENT) {
        free(dir);
        return -1;
    }
    int rc = mkdir(dir, 0755);
    free(dir);
    return rc;
}

static double percentile_from_sorted(const double *values, size_t n, double percentile) {
    if (!values || n == 0) {
        return 0.0;
    }
    if (n == 1) {
        return values[0];
    }
    double rank = percentile * (double)(n - 1);
    size_t lower = (size_t)rank;
    size_t upper = lower + 1;
    if (upper >= n) {
        return values[n - 1];
    }
    double weight = rank - (double)lower;
    return values[lower] + (values[upper] - values[lower]) * weight;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }
    return 0;
}

static int bench_vm_iteration(void *user_data) {
    bench_vm_ctx_t *ctx = (bench_vm_ctx_t *)user_data;
    vm_result_t result = {0};
    if (vm_run(&ctx->program, &ctx->limits, NULL, &result) != 0) {
        return -1;
    }
    return result.status == VM_OK ? 0 : -1;
}

static uint8_t digit_from_int(unsigned value) {
    return (uint8_t)(value % 10u);
}

static int bench_fkv_iteration(void *user_data) {
    bench_fkv_ctx_t *ctx = (bench_fkv_ctx_t *)user_data;
    fkv_iter_t it = {0};
    int rc = fkv_get_prefix(ctx->prefix, ctx->prefix_len, &it, ctx->limit);
    if (rc == 0) {
        fkv_iter_free(&it);
    }
    return rc;
}

static int bench_http_iteration(void *user_data) {
    bench_http_ctx_t *ctx = (bench_http_ctx_t *)user_data;
    http_response_t resp = {0};
    int rc = http_handle_request(&ctx->cfg, ctx->method, ctx->path, ctx->body, ctx->body ? strlen(ctx->body) : 0, &resp);
    http_response_free(&resp);
    return rc;
}

static int run_iterations(size_t warmup,
                          size_t iterations,
                          double *samples_ms,
                          int (*fn)(void *),
                          void *ctx) {
    struct timespec start, end;
    for (size_t i = 0; i < warmup; ++i) {
        if (fn(ctx) != 0) {
            return -1;
        }
    }
    for (size_t i = 0; i < iterations; ++i) {
        if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
            return -1;
        }
        if (fn(ctx) != 0) {
            return -1;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) {
            return -1;
        }
        samples_ms[i] = timespec_to_ms_diff(&start, &end);
    }
    return 0;
}

static void compute_stats(bench_result_t *result, double *samples, size_t count) {
    if (!result || !samples || count == 0) {
        return;
    }
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        sum += samples[i];
    }
    result->avg_ms = sum / (double)count;
    double *sorted = malloc(count * sizeof(double));
    if (!sorted) {
        result->p95_ms = 0.0;
        result->p99_ms = 0.0;
        result->samples = count;
        return;
    }
    memcpy(sorted, samples, count * sizeof(double));
    qsort(sorted, count, sizeof(double), cmp_double);
    result->p95_ms = percentile_from_sorted(sorted, count, 0.95);
    result->p99_ms = percentile_from_sorted(sorted, count, 0.99);
    result->samples = count;
    free(sorted);
}

static void report_to_console(const bench_result_t *result) {
    if (!result) {
        return;
    }
    if (result->status == 1) {
        log_warn("bench %-12s avg=%.4fms p95=%.4fms p99=%.4fms status=regression",
                 result->name,
                 result->avg_ms,
                 result->p95_ms,
                 result->p99_ms);
    } else if (result->status < 0) {
        log_error("bench %-12s avg=%.4fms p95=%.4fms p99=%.4fms status=error",
                  result->name,
                  result->avg_ms,
                  result->p95_ms,
                  result->p99_ms);
    } else {
        log_info("bench %-12s avg=%.4fms p95=%.4fms p99=%.4fms status=ok",
                 result->name,
                 result->avg_ms,
                 result->p95_ms,
                 result->p99_ms);
    }
}

static void write_json_report(FILE *fp,
                              const bench_options_t *opts,
                              const bench_result_t *results,
                              size_t result_count,
                              int regression) {
    if (!fp) {
        return;
    }
    time_t now = time(NULL);
    fprintf(fp, "{\n");
    fprintf(fp, "  \"generated_at\": %lld,\n", (long long)now);
    fprintf(fp, "  \"options\": {\n");
    fprintf(fp, "    \"iterations\": %zu,\n", opts->iterations);
    fprintf(fp, "    \"warmup\": %zu,\n", opts->warmup);
    fprintf(fp, "    \"profile\": %s\n", opts->include_profile ? "true" : "false");
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"regression\": %s,\n", regression ? "true" : "false");
    fprintf(fp, "  \"benchmarks\": [\n");
    for (size_t i = 0; i < result_count; ++i) {
        const bench_result_t *r = &results[i];
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"name\": \"%s\",\n", r->name);
        fprintf(fp, "      \"samples\": %zu,\n", r->samples);
        fprintf(fp, "      \"avg_ms\": %.6f,\n", r->avg_ms);
        fprintf(fp, "      \"p95_ms\": %.6f,\n", r->p95_ms);
        fprintf(fp, "      \"p99_ms\": %.6f,\n", r->p99_ms);
        fprintf(fp, "      \"thresholds\": { \"p95_ms\": %.6f, \"p99_ms\": %.6f },\n",
                r->threshold_p95_ms,
                r->threshold_p99_ms);
        fprintf(fp, "      \"status\": \"%s\"",
                (r->status == 0) ? "ok" : (r->status == 1) ? "regression" : "error");
        if (opts->include_profile && r->profile_ms && r->samples > 0) {
            fprintf(fp, ",\n      \"samples_ms\": [");
            for (size_t j = 0; j < r->samples; ++j) {
                fprintf(fp, "%s%.6f", j == 0 ? "" : ",", r->profile_ms[j]);
            }
            fprintf(fp, "]\n");
        } else {
            fprintf(fp, "\n");
        }
        fprintf(fp, "    }%s\n", (i + 1 < result_count) ? "," : "");
    }
    fprintf(fp, "  ]\n");
    fprintf(fp, "}\n");
}

static void populate_vm_program(bench_vm_ctx_t *ctx) {
    static uint8_t code[] = {
        0x01, 2, 0x01, 3, 0x02, 0x01, 5, 0x04, 0x01, 1, 0x03, 0x12
    };
    ctx->program.code = code;
    ctx->program.len = ARRAY_SIZE(code);
    ctx->limits.max_stack = 64;
    ctx->limits.max_steps = 256;
}

static int populate_fkv(bench_fkv_ctx_t *ctx) {
    ctx->key_len = 4;
    ctx->value_len = 8;
    ctx->key_count = 100;
    ctx->prefix_len = 3;
    ctx->prefix[0] = 1;
    ctx->prefix[1] = 2;
    ctx->prefix[2] = 3;
    ctx->limit = 16;
    if (fkv_init() != 0) {
        return -1;
    }
    for (size_t i = 0; i < ctx->key_count; ++i) {
        uint8_t key[4];
        key[0] = ctx->prefix[0];
        key[1] = ctx->prefix[1];
        key[2] = ctx->prefix[2];
        key[3] = digit_from_int((unsigned)i);
        uint8_t value[8];
        for (size_t j = 0; j < ctx->value_len; ++j) {
            value[j] = digit_from_int((unsigned)(i + j));
        }
        if (fkv_put(key, ctx->key_len, value, ctx->value_len, FKV_ENTRY_TYPE_VALUE) != 0) {
            return -1;
        }
    }
    return 0;
}

static void teardown_fkv(void) {
    fkv_shutdown();
}

static void populate_http_ctx(bench_http_ctx_t *ctx, const kolibri_config_t *cfg) {
    memset(&ctx->cfg, 0, sizeof(ctx->cfg));
    if (cfg) {
        ctx->cfg = *cfg;
    }
    ctx->method = "POST";
    ctx->path = "/api/v1/dialog";
    ctx->body = "{\"input\":\"ping\"}";
}

void bench_options_init(bench_options_t *opts) {
    if (!opts) {
        return;
    }
    opts->iterations = 200;
    opts->warmup = 20;
    opts->output_path = NULL;
    opts->include_profile = 0;
    opts->override_count = 0;
}

int bench_options_add_threshold_override(bench_options_t *opts,
                                         const char *name,
                                         int has_p95,
                                         double p95_ms,
                                         int has_p99,
                                         double p99_ms) {
    if (!opts || !name || (!has_p95 && !has_p99)) {
        return -1;
    }
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len >= sizeof(opts->overrides[0].name)) {
        return -1;
    }
    for (size_t i = 0; i < opts->override_count; ++i) {
        bench_threshold_override_t *ov = &opts->overrides[i];
        if (strcmp(ov->name, name) == 0) {
            if (has_p95) {
                ov->has_p95 = 1;
                ov->p95_ms = p95_ms;
            }
            if (has_p99) {
                ov->has_p99 = 1;
                ov->p99_ms = p99_ms;
            }
            return 0;
        }
    }
    if (opts->override_count >= BENCH_MAX_OVERRIDES) {
        return -1;
    }
    bench_threshold_override_t *ov = &opts->overrides[opts->override_count++];
    memset(ov, 0, sizeof(*ov));
    memcpy(ov->name, name, name_len);
    ov->name[name_len] = '\0';
    if (has_p95) {
        ov->has_p95 = 1;
        ov->p95_ms = p95_ms;
    }
    if (has_p99) {
        ov->has_p99 = 1;
        ov->p99_ms = p99_ms;
    }
    return 0;
}

int bench_run_all(const kolibri_config_t *cfg, const bench_options_t *opts) {
    if (!opts) {
        return -1;
    }

    bench_result_t results[3];
    memset(results, 0, sizeof(results));

    bench_vm_ctx_t vm_ctx;
    populate_vm_program(&vm_ctx);

    bench_fkv_ctx_t fkv_ctx;
    if (populate_fkv(&fkv_ctx) != 0) {
        log_error("failed to set up F-KV benchmark data");
        teardown_fkv();
        return -1;
    }

    bench_http_ctx_t http_ctx;
    populate_http_ctx(&http_ctx, cfg);

    double *delta_vm_samples = calloc(opts->iterations, sizeof(double));
    double *delta_vm_profile = NULL;
    if (!delta_vm_samples) {
        teardown_fkv();
        return -1;
    }
    memcpy(&results[0],
           &(bench_result_t){
               .name = "delta_vm",
               .threshold_p95_ms = 50.0,
               .threshold_p99_ms = 70.0,
           },
           sizeof(bench_result_t));
    apply_threshold_override(opts, &results[0]);

    if (run_iterations(opts->warmup, opts->iterations, delta_vm_samples, bench_vm_iteration, &vm_ctx) != 0) {
        results[0].status = -1;
    } else {
        compute_stats(&results[0], delta_vm_samples, opts->iterations);
        if (opts->include_profile) {
            delta_vm_profile = calloc(opts->iterations, sizeof(double));
            if (delta_vm_profile) {
                memcpy(delta_vm_profile, delta_vm_samples, opts->iterations * sizeof(double));
            }
        }
        if (results[0].p95_ms > results[0].threshold_p95_ms ||
            results[0].p99_ms > results[0].threshold_p99_ms) {
            results[0].status = 1;
        }
    }

    double *fkv_samples = calloc(opts->iterations, sizeof(double));
    double *fkv_profile = NULL;
    if (!fkv_samples) {
        free(delta_vm_samples);
        free(delta_vm_profile);
        teardown_fkv();
        return -1;
    }
    memcpy(&results[1],
           &(bench_result_t){
               .name = "fkv_prefix_get",
               .threshold_p95_ms = 10.0,
               .threshold_p99_ms = 20.0,
           },
           sizeof(bench_result_t));
    apply_threshold_override(opts, &results[1]);

    if (run_iterations(opts->warmup, opts->iterations, fkv_samples, bench_fkv_iteration, &fkv_ctx) != 0) {
        results[1].status = -1;
    } else {
        compute_stats(&results[1], fkv_samples, opts->iterations);
        if (opts->include_profile) {
            fkv_profile = calloc(opts->iterations, sizeof(double));
            if (fkv_profile) {
                memcpy(fkv_profile, fkv_samples, opts->iterations * sizeof(double));
            }
        }
        if (results[1].p95_ms > results[1].threshold_p95_ms ||
            results[1].p99_ms > results[1].threshold_p99_ms) {
            results[1].status = 1;
        }
    }

    double *http_samples = calloc(opts->iterations, sizeof(double));
    double *http_profile = NULL;
    if (!http_samples) {
        free(delta_vm_samples);
        free(delta_vm_profile);
        free(fkv_samples);
        free(fkv_profile);
        teardown_fkv();
        return -1;
    }
    memcpy(&results[2],
           &(bench_result_t){
               .name = "http_dialog",
               .threshold_p95_ms = 30.0,
               .threshold_p99_ms = 50.0,
           },
           sizeof(bench_result_t));
    apply_threshold_override(opts, &results[2]);

    if (run_iterations(opts->warmup, opts->iterations, http_samples, bench_http_iteration, &http_ctx) != 0) {
        results[2].status = -1;
    } else {
        compute_stats(&results[2], http_samples, opts->iterations);
        if (opts->include_profile) {
            http_profile = calloc(opts->iterations, sizeof(double));
            if (http_profile) {
                memcpy(http_profile, http_samples, opts->iterations * sizeof(double));
            }
        }
        if (results[2].p95_ms > results[2].threshold_p95_ms ||
            results[2].p99_ms > results[2].threshold_p99_ms) {
            results[2].status = 1;
        }
    }

    teardown_fkv();

    int regression = 0;
    for (size_t i = 0; i < ARRAY_SIZE(results); ++i) {
        if (results[i].status == 1) {
            regression = 1;
        }
        report_to_console(&results[i]);
    }

    if (opts->output_path) {
        if (ensure_parent_dir(opts->output_path) != 0) {
            log_warn("failed to prepare directory for %s", opts->output_path);
        }
        FILE *fp = fopen(opts->output_path, "w");
        if (!fp) {
            log_error("failed to open %s for writing", opts->output_path);
        } else {
            results[0].profile_ms = delta_vm_profile;
            results[1].profile_ms = fkv_profile;
            results[2].profile_ms = http_profile;
            write_json_report(fp, opts, results, ARRAY_SIZE(results), regression);
            fclose(fp);
            log_info("benchmark report saved to %s", opts->output_path);
        }
    }

    free(delta_vm_samples);
    free(fkv_samples);
    free(http_samples);
    free(delta_vm_profile);
    free(fkv_profile);
    free(http_profile);

    if (regression) {
        log_warn("benchmark regression detected");
        return 2;
    }
    for (size_t i = 0; i < ARRAY_SIZE(results); ++i) {
        if (results[i].status < 0) {
            return -1;
        }
    }
    return 0;
}
