


/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#define _POSIX_C_SOURCE 200809L
#include "blockchain.h"
#include "fkv/fkv.h"
#include "http/http_routes.h"
#include "http/http_server.h"
#include "kolibri_ai.h"
#include "synthesis/formula_vm_eval.h"
#include "util/config.h"
#include "util/log.h"
#include "vm/vm.h"
#include "formula.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

static char *trim_inplace(char *line) {
    if (!line) {
        return NULL;
    }
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
    size_t start = 0;
    while (line[start] && isspace((unsigned char)line[start])) {
        start++;
    }
    if (start > 0 && line[start]) {
        memmove(line, line + start, strlen(line + start) + 1);
    } else if (start > 0) {
        line[0] = '\0';
    }
    len = strlen(line);
    while (len > 0 && isspace((unsigned char)line[len - 1])) {
        line[--len] = '\0';
    }
    return line;
}

static int append_digit(uint8_t **buf, size_t *cap, size_t *len, uint8_t value) {
    if (!buf || !cap || !len) {
        return -1;
    }
    if (*len >= *cap) {
        size_t new_cap = (*cap == 0) ? 16 : (*cap * 2);
        uint8_t *tmp = realloc(*buf, new_cap);
        if (!tmp) {
            return -1;
        }
        *buf = tmp;
        *cap = new_cap;
    }
    (*buf)[(*len)++] = value;
    return 0;
}

static int parse_expression_digits(const char *input, uint8_t **out_digits, size_t *out_len, int *saw_operator) {
    if (!input || !out_digits || !out_len) {
        return -1;
    }
    uint8_t *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    int has_operator = 0;
    for (const char *p = input; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (isspace(ch)) {
            continue;
        }
        if (isdigit(ch)) {
            if (append_digit(&buf, &cap, &len, (uint8_t)(ch - '0')) != 0) {
                free(buf);
                return -1;
            }
            continue;
        }
        if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
            if (append_digit(&buf, &cap, &len, (uint8_t)ch) != 0) {
                free(buf);
                return -1;
            }
            has_operator = 1;
            continue;
        }
        free(buf);
        return -1;
    }
    if (len == 0) {
        free(buf);
        return -1;
    }
    *out_digits = buf;
    *out_len = len;
    if (saw_operator) {
        *saw_operator = has_operator;
    }
    return 0;
}

static int digits_from_number(uint64_t value, uint8_t *out, size_t capacity, size_t *out_len) {
    if (!out || !out_len || capacity == 0) {
        return -1;
    }
    size_t len = 0;
    do {
        if (len >= capacity) {
            return -1;
        }
        out[len++] = (uint8_t)(value % 10u);
        value /= 10u;
    } while (value > 0u);
    for (size_t i = 0; i < len / 2; ++i) {
        uint8_t tmp = out[i];
        out[i] = out[len - 1 - i];
        out[len - 1 - i] = tmp;
    }
    *out_len = len;
    return 0;
}

static int try_evaluate_expression(const kolibri_config_t *cfg,
                                   const char *input,
                                   uint64_t *out_value,
                                   uint32_t *out_steps) {
    if (!input) {
        return -1;
    }
    uint8_t *digits = NULL;
    size_t digits_len = 0;
    int has_operator = 0;
    if (parse_expression_digits(input, &digits, &digits_len, &has_operator) != 0) {
        return -1;
    }
    if (!has_operator) {
        free(digits);
        return -1;
    }
    uint8_t *bytecode = NULL;
    size_t bytecode_len = 0;
    if (formula_vm_compile_from_digits(digits, digits_len, &bytecode, &bytecode_len) != 0) {
        free(digits);
        return -1;
    }
    free(digits);

    vm_limits_t limits = {0};
    if (cfg) {
        limits.max_steps = cfg->vm.max_steps ? cfg->vm.max_steps : 256u;
        limits.max_stack = cfg->vm.max_stack ? cfg->vm.max_stack : 64u;
    } else {
        limits.max_steps = 256u;
        limits.max_stack = 64u;
    }

    prog_t prog = {.code = bytecode, .len = bytecode_len};
    vm_result_t result = {0};
    vm_run(&prog, &limits, NULL, &result);
    free(bytecode);

    if (result.status != VM_OK) {
        return -1;
    }

    if (out_value) {
        *out_value = result.result;
    }
    if (out_steps) {
        *out_steps = result.steps;
    }
    return 0;
}

static void record_interaction(KolibriAI *ai,
                               const char *prompt,
                               double reward,
                               int success,
                               double expected_result) {
    if (!ai || !prompt) {
        return;
    }
    KolibriAISelfplayInteraction interaction = {0};
    interaction.task.difficulty = 1;
    snprintf(interaction.task.description,
             sizeof(interaction.task.description),
             "cli:%s",
             prompt);
    interaction.task.expected_result = expected_result;
    interaction.predicted_result = expected_result;
    interaction.error = success ? 0.0 : 1.0;
    interaction.reward = reward;
    interaction.success = success;
    kolibri_ai_record_interaction(ai, &interaction);
}

static void describe_best_formula(KolibriAI *ai) {
    if (!ai) {
        printf("kolibri> Пока не готов отвечать — библиотека знаний ещё пустая.\n");
        return;
    }
    Formula *best = kolibri_ai_get_best_formula(ai);
    if (!best) {
        printf("kolibri> Ещё думаю над новыми формулами. Попробуй арифметику!\n");
        return;
    }
    if (best->representation == FORMULA_REPRESENTATION_TEXT && best->content[0]) {
        printf("kolibri> Лучшая формула в библиотеке: %s\n", best->content);
    } else {
        printf("kolibri> В библиотеке есть полезные формулы, но ответ пока не найден.\n");
    }
    formula_clear(best);
    free(best);
}

typedef struct {
    double mean_us;
    double p95_us;
    double min_us;
    double max_us;
    double stddev_us;
} bench_timing_stats_t;

#define MAX_VM_BENCH_CASES 8
#define BENCH_ERROR_MSG_MAX 128

typedef struct {
    char name[32];
    size_t iterations;
    size_t completed;
    uint64_t expected_result;
    uint64_t actual_result;
    double halt_ratio;
    double avg_steps;
    uint32_t min_steps;
    uint32_t max_steps;
    bench_timing_stats_t timings;
    double throughput_ops;
    int result_mismatch;
    int ok;
    char error[BENCH_ERROR_MSG_MAX];
} bench_vm_case_report_t;

typedef struct {
    size_t operations;
    size_t put_completed;
    size_t get_completed;
    size_t hits;
    size_t value_mismatches;
    double hit_rate;
    bench_timing_stats_t put_timings;
    bench_timing_stats_t get_timings;
    double put_throughput_ops;
    double get_throughput_ops;
    int ok;
    char error[BENCH_ERROR_MSG_MAX];
} bench_fkv_report_t;

typedef struct {
    bench_vm_case_report_t vm_cases[MAX_VM_BENCH_CASES];
    size_t vm_count;
    bench_fkv_report_t fkv;
    int has_fkv;
    int overall_ok;
} bench_report_t;

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void bench_log_line(FILE *fp, const char *fmt, ...) {
    if (!fmt) {
        return;
    }
    char buffer[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    log_info("%s", buffer);
    if (fp) {
        fprintf(fp, "%s\n", buffer);
        fflush(fp);
    }
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

static int compute_timing_stats(const double *samples, size_t count, bench_timing_stats_t *out) {
    if (!samples || count == 0 || !out) {
        return -1;
    }
    double min_v = samples[0];
    double max_v = samples[0];
    double sum = 0.0;
    double sum_sq = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double v = samples[i];
        if (v < min_v) {
            min_v = v;
        }
        if (v > max_v) {
            max_v = v;
        }
        sum += v;
        sum_sq += v * v;
    }
    double *sorted = malloc(count * sizeof(double));
    if (!sorted) {
        return -1;
    }
    memcpy(sorted, samples, count * sizeof(double));
    qsort(sorted, count, sizeof(double), cmp_double);
    size_t rank = (95 * count + 99) / 100;
    if (rank == 0) {
        rank = 1;
    }
    if (rank > count) {
        rank = count;
    }
    out->mean_us = sum / (double)count;
    out->p95_us = sorted[rank - 1];
    out->min_us = min_v;
    out->max_us = max_v;
    double variance = sum_sq / (double)count - out->mean_us * out->mean_us;
    if (variance < 0.0) {
        variance = 0.0;
    }
    out->stddev_us = sqrt(variance);
    free(sorted);
    return 0;
}

static void compute_step_stats(const uint32_t *values,
                               size_t count,
                               double *avg_out,
                               uint32_t *min_out,
                               uint32_t *max_out) {
    if (!values || count == 0) {
        if (avg_out) {
            *avg_out = 0.0;
        }
        if (min_out) {
            *min_out = 0;
        }
        if (max_out) {
            *max_out = 0;
        }
        return;
    }
    uint32_t min_v = values[0];
    uint32_t max_v = values[0];
    uint64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t v = values[i];
        if (v < min_v) {
            min_v = v;
        }
        if (v > max_v) {
            max_v = v;
        }
        sum += (uint64_t)v;
    }
    if (avg_out) {
        *avg_out = (double)sum / (double)count;
    }
    if (min_out) {
        *min_out = min_v;
    }
    if (max_out) {
        *max_out = max_v;
    }
}

static int build_program_from_expression(const char *expression,
                                         uint8_t **out_code,
                                         size_t *out_len) {
    if (!expression || !out_code || !out_len) {
        return -1;
    }
    *out_code = NULL;
    *out_len = 0;
    if (formula_vm_compile_from_text(expression, out_code, out_len) != 0) {
        return -1;
    }
    if (*out_len > 0 && (*out_code)[*out_len - 1] == 0x0B) {
        (*out_len)--;
    }
    uint8_t *resized = realloc(*out_code, *out_len + 1);
    if (!resized) {
        free(*out_code);
        *out_code = NULL;
        *out_len = 0;
        return -1;
    }
    resized[*out_len] = 0x12; // HALT
    *out_code = resized;
    *out_len = *out_len + 1;
    return 0;
}

static int run_vm_microbench(FILE *log_fp, bench_report_t *report) {
    typedef struct {
        const char *name;
        const char *expression;
        size_t iterations;
        uint64_t expected_result;
    } vm_case_t;

    const vm_case_t cases[] = {
        {"add_small", "2+3", 1000, 5u},
        {"mul_large", "98765*4321", 1000, 426763565u},
        {"div_long", "123456789/3", 1000, 41152263u},
    };

    bench_log_line(log_fp, "--- Δ-VM microbenchmarks ---");
    int rc = 0;
    vm_limits_t limits = {.max_steps = 512, .max_stack = 128};

    const size_t case_count = sizeof(cases) / sizeof(cases[0]);
    if (report) {
        if (case_count > MAX_VM_BENCH_CASES) {
            bench_log_line(log_fp,
                           "Δ-VM | configuration error: %zu cases exceed MAX_VM_BENCH_CASES=%d",
                           case_count,
                           MAX_VM_BENCH_CASES);
            report->vm_count = MAX_VM_BENCH_CASES;
        } else {
            report->vm_count = case_count;
        }
        size_t to_init = report->vm_count;
        for (size_t i = 0; i < to_init; ++i) {
            bench_vm_case_report_t *slot = &report->vm_cases[i];
            memset(slot, 0, sizeof(*slot));
            if (i < case_count) {
                snprintf(slot->name, sizeof(slot->name), "%s", cases[i].name);
                slot->iterations = cases[i].iterations;
                slot->expected_result = cases[i].expected_result;
            }
            slot->ok = 1;
        }
    }

    for (size_t c = 0; c < case_count; ++c) {
        const vm_case_t *vm_case = &cases[c];
        bench_vm_case_report_t *slot = NULL;
        if (report && c < report->vm_count) {
            slot = &report->vm_cases[c];
            slot->completed = 0;
            slot->actual_result = 0;
            slot->avg_steps = 0.0;
            slot->min_steps = 0;
            slot->max_steps = 0;
            slot->halt_ratio = 0.0;
            slot->result_mismatch = 0;
            slot->throughput_ops = 0.0;
            slot->timings.mean_us = 0.0;
            slot->timings.p95_us = 0.0;
            slot->timings.min_us = 0.0;
            slot->timings.max_us = 0.0;
            slot->timings.stddev_us = 0.0;
            slot->error[0] = '\0';
        }
        uint8_t *code = NULL;
        size_t code_len = 0;
        if (build_program_from_expression(vm_case->expression, &code, &code_len) != 0) {
            bench_log_line(log_fp,
                           "Δ-VM %-12s | failed to compile expression '%s'",
                           vm_case->name,
                           vm_case->expression);
            if (slot) {
                slot->ok = 0;
                snprintf(slot->error,
                         sizeof(slot->error),
                         "compile failed for '%s'",
                         vm_case->expression);
            }
            rc = -1;
            continue;
        }

        double *samples = calloc(vm_case->iterations, sizeof(double));
        uint32_t *steps = calloc(vm_case->iterations, sizeof(uint32_t));
        if (!samples || !steps) {
            bench_log_line(log_fp,
                           "Δ-VM %-12s | memory allocation failed",
                           vm_case->name);
            free(samples);
            free(steps);
            free(code);
            if (slot) {
                slot->ok = 0;
                snprintf(slot->error,
                         sizeof(slot->error),
                         "allocation failed for %zu iterations",
                         vm_case->iterations);
            }
            rc = -1;
            continue;
        }

        size_t completed = 0;
        size_t halted_count = 0;
        int result_mismatch = 0;
        uint64_t last_result = 0;
        for (; completed < vm_case->iterations; ++completed) {
            prog_t prog = {.code = code, .len = code_len};
            uint64_t start_ns = monotonic_ns();
            vm_result_t result = {0};
            int run_rc = vm_run(&prog, &limits, NULL, &result);
            uint64_t end_ns = monotonic_ns();
            if (run_rc != 0 || result.status != VM_OK) {
                bench_log_line(log_fp,
                               "Δ-VM %-12s | iteration %zu failed (rc=%d status=%d)",
                               vm_case->name,
                               completed,
                               run_rc,
                               (int)result.status);
                if (slot) {
                    slot->ok = 0;
                    snprintf(slot->error,
                             sizeof(slot->error),
                             "iteration %zu failed (rc=%d status=%d)",
                             completed,
                             run_rc,
                             (int)result.status);
                }
                rc = -1;
                break;
            }
            samples[completed] = (double)(end_ns - start_ns) / 1000.0; // microseconds
            steps[completed] = result.steps;
            if (result.halted) {
                halted_count++;
            }
            if (result.result != vm_case->expected_result) {
                result_mismatch = 1;
            }
            last_result = result.result;
            if (slot) {
                slot->actual_result = result.result;
                slot->completed = completed + 1;
            }
        }

        if (completed == vm_case->iterations) {
            bench_timing_stats_t stats;
            if (compute_timing_stats(samples, vm_case->iterations, &stats) != 0) {
                bench_log_line(log_fp,
                               "Δ-VM %-12s | failed to compute timing stats",
                               vm_case->name);
                if (slot) {
                    slot->ok = 0;
                    snprintf(slot->error,
                             sizeof(slot->error),
                             "timing stats failed");
                }
                rc = -1;
            } else {
                double avg_steps = 0.0;
                uint32_t min_steps = 0;
                uint32_t max_steps = 0;
                compute_step_stats(steps,
                                   vm_case->iterations,
                                   &avg_steps,
                                   &min_steps,
                                   &max_steps);
                double halted_ratio = vm_case->iterations
                                           ? (double)halted_count * 100.0 /
                                                 (double)vm_case->iterations
                                           : 0.0;
                bench_log_line(log_fp,
                               "Δ-VM %-12s | iters=%zu | mean=%.2f µs | p95=%.2f µs | min=%.2f µs | max=%.2f µs | stddev=%.2f µs | throughput=%.0f ops/s | steps(avg)=%.2f min=%u max=%u | HALT=%.1f%% | result=%" PRIu64,
                               vm_case->name,
                               vm_case->iterations,
                               stats.mean_us,
                               stats.p95_us,
                               stats.min_us,
                               stats.max_us,
                               stats.stddev_us,
                               stats.mean_us > 0.0 ? 1000000.0 / stats.mean_us : 0.0,
                               avg_steps,
                               min_steps,
                               max_steps,
                               halted_ratio,
                               last_result);
                if (result_mismatch) {
                    bench_log_line(log_fp,
                                   "Δ-VM %-12s | result mismatch detected (expected=%" PRIu64 ")",
                                   vm_case->name,
                                   vm_case->expected_result);
                    if (slot && slot->error[0] == '\0') {
                        snprintf(slot->error,
                                 sizeof(slot->error),
                                 "result mismatch (expected=%" PRIu64 ")",
                                 vm_case->expected_result);
                    }
                    rc = -1;
                }
                if (slot) {
                    slot->timings = stats;
                    slot->avg_steps = avg_steps;
                    slot->min_steps = min_steps;
                    slot->max_steps = max_steps;
                    slot->halt_ratio = halted_ratio;
                    slot->result_mismatch = result_mismatch;
                    slot->actual_result = last_result;
                    slot->throughput_ops = stats.mean_us > 0.0 ? 1000000.0 / stats.mean_us : 0.0;
                    if (result_mismatch) {
                        slot->ok = 0;
                    }
                }
            }
        } else if (slot) {
            slot->ok = 0;
            if (slot->error[0] == '\0') {
                snprintf(slot->error,
                         sizeof(slot->error),
                         "completed %zu/%zu iterations",
                         completed,
                         vm_case->iterations);
            }
            slot->completed = completed;
        }
        free(samples);
        free(steps);
        free(code);
    }
    return rc;
}

static int run_fkv_microbench(FILE *log_fp, bench_report_t *report) {
    const size_t operations = 1000;
    bench_log_line(log_fp, "--- F-KV microbenchmarks ---");

    if (report) {
        report->has_fkv = 1;
        memset(&report->fkv, 0, sizeof(report->fkv));
        report->fkv.operations = operations;
        report->fkv.ok = 1;
        report->fkv.error[0] = '\0';
    }

    if (fkv_init() != 0) {
        bench_log_line(log_fp, "F-KV init failed");
        if (report) {
            report->fkv.ok = 0;
            snprintf(report->fkv.error, sizeof(report->fkv.error), "init failed");
        }
        return -1;
    }

    double *put_samples = calloc(operations, sizeof(double));
    double *get_samples = calloc(operations, sizeof(double));
    if (!put_samples || !get_samples) {
        bench_log_line(log_fp, "F-KV | memory allocation failed");
        free(put_samples);
        free(get_samples);
        fkv_shutdown();
        if (report) {
            report->fkv.ok = 0;
            snprintf(report->fkv.error, sizeof(report->fkv.error), "allocation failed");
        }
        return -1;
    }

    int rc = 0;
    size_t put_completed = 0;
    int fatal_put_error = 0;
    for (size_t i = 0; i < operations; ++i) {
        uint8_t key_digits[32];
        size_t key_len = 0;
        uint8_t value_digits[32];
        size_t value_len = 0;
        if (digits_from_number(1000u + (uint64_t)i, key_digits, sizeof(key_digits), &key_len) != 0 ||
            digits_from_number((uint64_t)i * 17u + 11u,
                               value_digits,
                               sizeof(value_digits),
                               &value_len) != 0) {
            bench_log_line(log_fp, "F-KV PUT | digit encoding failed at %zu", i);
            rc = -1;
            fatal_put_error = 1;
            if (report && report->fkv.error[0] == '\0') {
                snprintf(report->fkv.error,
                         sizeof(report->fkv.error),
                         "digit encoding failed at put %zu",
                         i);
            }
            break;
        }
        uint64_t start_ns = monotonic_ns();
        int put_rc = fkv_put(key_digits, key_len, value_digits, value_len, FKV_ENTRY_TYPE_VALUE);
        uint64_t end_ns = monotonic_ns();
        if (put_rc != 0) {
            bench_log_line(log_fp, "F-KV PUT | operation %zu failed (rc=%d)", i, put_rc);
            rc = -1;
            fatal_put_error = 1;
            if (report && report->fkv.error[0] == '\0') {
                snprintf(report->fkv.error,
                         sizeof(report->fkv.error),
                         "put failure at %zu (rc=%d)",
                         i,
                         put_rc);
            }
            break;
        }
        put_samples[put_completed++] = (double)(end_ns - start_ns) / 1000.0;
    }

    size_t hits = 0;
    size_t value_mismatches = 0;
    size_t get_completed = 0;
    int fatal_get_error = 0;
    if (!fatal_put_error && put_completed == operations) {
        for (size_t i = 0; i < operations; ++i) {
            uint8_t key_digits[32];
            size_t key_len = 0;
            if (digits_from_number(1000u + (uint64_t)i,
                                   key_digits,
                                   sizeof(key_digits),
                                   &key_len) != 0) {
                bench_log_line(log_fp, "F-KV GET | digit encoding failed at %zu", i);
                rc = -1;
                fatal_get_error = 1;
                if (report && report->fkv.error[0] == '\0') {
                    snprintf(report->fkv.error,
                             sizeof(report->fkv.error),
                             "digit encoding failed at get %zu",
                             i);
                }
                break;
            }
            uint64_t start_ns = monotonic_ns();
            fkv_iter_t it = {0};
            int get_rc = fkv_get_prefix(key_digits, key_len, &it, 1);
            uint64_t end_ns = monotonic_ns();
            if (get_rc != 0) {
                bench_log_line(log_fp, "F-KV GET | operation %zu failed (rc=%d)", i, get_rc);
                rc = -1;
                fatal_get_error = 1;
                if (report && report->fkv.error[0] == '\0') {
                    snprintf(report->fkv.error,
                             sizeof(report->fkv.error),
                             "get failure at %zu (rc=%d)",
                             i,
                             get_rc);
                }
                fkv_iter_free(&it);
                break;
            }
            get_samples[get_completed++] = (double)(end_ns - start_ns) / 1000.0;
            if (it.count > 0) {
                hits++;
                uint8_t expected_digits[32];
                size_t expected_len = 0;
                if (digits_from_number((uint64_t)i * 17u + 11u,
                                       expected_digits,
                                       sizeof(expected_digits),
                                       &expected_len) == 0) {
                    const fkv_entry_t *entry = &it.entries[0];
                    int mismatch = 0;
                    if (entry->type != FKV_ENTRY_TYPE_VALUE || entry->value_len != expected_len ||
                        memcmp(entry->value, expected_digits, expected_len) != 0) {
                        mismatch = 1;
                    }
                    if (mismatch) {
                        value_mismatches++;
                        if (rc == 0) {
                            rc = -1;
                        }
                        if (report && report->fkv.error[0] == '\0') {
                            snprintf(report->fkv.error,
                                     sizeof(report->fkv.error),
                                     "value mismatch at %zu",
                                     i);
                        }
                    }
                }
            }
            fkv_iter_free(&it);
        }
    }

    if (value_mismatches > 0) {
        bench_log_line(log_fp,
                       "F-KV GET | detected %zu mismatched values",
                       value_mismatches);
    }

    if (!fatal_put_error && put_completed == operations && !fatal_get_error && get_completed == operations) {
        bench_timing_stats_t put_stats;
        bench_timing_stats_t get_stats;
        if (compute_timing_stats(put_samples, operations, &put_stats) != 0 ||
            compute_timing_stats(get_samples, operations, &get_stats) != 0) {
            bench_log_line(log_fp, "F-KV | failed to compute timing stats");
            rc = -1;
        } else {
            bench_log_line(log_fp,
                           "F-KV PUT  | ops=%zu | mean=%.2f µs | p95=%.2f µs | min=%.2f µs | max=%.2f µs | stddev=%.2f µs | throughput=%.0f ops/s",
                           operations,
                           put_stats.mean_us,
                           put_stats.p95_us,
                           put_stats.min_us,
                           put_stats.max_us,
                           put_stats.stddev_us,
                           put_stats.mean_us > 0.0 ? 1000000.0 / put_stats.mean_us : 0.0);
            double hit_rate = operations ? (double)hits * 100.0 / (double)operations : 0.0;
            bench_log_line(log_fp,
                           "F-KV GET  | ops=%zu | mean=%.2f µs | p95=%.2f µs | min=%.2f µs | max=%.2f µs | stddev=%.2f µs | throughput=%.0f ops/s | hit=%.1f%%",
                           operations,
                           get_stats.mean_us,
                           get_stats.p95_us,
                           get_stats.min_us,
                           get_stats.max_us,
                           get_stats.stddev_us,
                           get_stats.mean_us > 0.0 ? 1000000.0 / get_stats.mean_us : 0.0,
                           hit_rate);
            if (report) {
                report->fkv.put_timings = put_stats;
                report->fkv.get_timings = get_stats;
                report->fkv.put_throughput_ops = put_stats.mean_us > 0.0 ? 1000000.0 / put_stats.mean_us : 0.0;
                report->fkv.get_throughput_ops = get_stats.mean_us > 0.0 ? 1000000.0 / get_stats.mean_us : 0.0;
            }
        }
    }

    free(put_samples);
    free(get_samples);
    fkv_shutdown();

    if (report) {
        report->fkv.put_completed = put_completed;
        report->fkv.get_completed = get_completed;
        report->fkv.hits = hits;
        report->fkv.value_mismatches = value_mismatches;
        report->fkv.hit_rate = operations ? (double)hits * 100.0 / (double)operations : 0.0;
        if (rc == 0) {
            report->fkv.error[0] = '\0';
        } else if (report->fkv.error[0] == '\0') {
            snprintf(report->fkv.error, sizeof(report->fkv.error), "see logs");
        }
        report->fkv.ok = (rc == 0);
    }
    return rc;
}

static void json_write_string(FILE *fp, const char *str) {
    if (!fp) {
        return;
    }
    fputc('"', fp);
    if (!str) {
        str = "";
    }
    for (const unsigned char *p = (const unsigned char *)str; *p; ++p) {
        unsigned char ch = *p;
        switch (ch) {
        case '"':
            fputs("\\\"", fp);
            break;
        case '\\':
            fputs("\\\\", fp);
            break;
        case '\b':
            fputs("\\b", fp);
            break;
        case '\f':
            fputs("\\f", fp);
            break;
        case '\n':
            fputs("\\n", fp);
            break;
        case '\r':
            fputs("\\r", fp);
            break;
        case '\t':
            fputs("\\t", fp);
            break;
        default:
            if (ch < 0x20 || ch > 0x7E) {
                fprintf(fp, "\\u%04x", ch);
            } else {
                fputc((int)ch, fp);
            }
            break;
        }
    }
    fputc('"', fp);
}

static int write_bench_json(const char *path, const bench_report_t *report) {
    if (!path || !report) {
        errno = EINVAL;
        return -1;
    }

    char tmp_path[512];
    int written = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (written < 0 || (size_t)written >= sizeof(tmp_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        return -1;
    }

    time_t now = time(NULL);
    struct tm tm_buf;
    char timestamp[64];
    if (gmtime_r(&now, &tm_buf) != NULL &&
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_buf) > 0) {
        // timestamp ready
    } else {
        snprintf(timestamp, sizeof(timestamp), "unknown");
    }

    fprintf(fp, "{\n  \"status\": \"%s\",\n  \"timestamp\": ", report->overall_ok ? "ok" : "error");
    json_write_string(fp, timestamp);
    fprintf(fp, ",\n  \"vm\": [\n");

    for (size_t i = 0; i < report->vm_count; ++i) {
        const bench_vm_case_report_t *vm = &report->vm_cases[i];
        fprintf(fp,
                "    {\n      \"name\": ");
        json_write_string(fp, vm->name);
        fprintf(fp,
                ",\n      \"iterations\": %zu,\n      \"completed\": %zu,\n      \"expected_result\": %" PRIu64 ",\n      \"actual_result\": %" PRIu64 ",\n      \"result_mismatch\": %s,\n      \"halt_ratio\": %.4f,\n      \"throughput_ops\": %.2f,\n      \"steps\": {\n        \"avg\": %.2f,\n        \"min\": %u,\n        \"max\": %u\n      },\n      \"timing\": {\n        \"mean_us\": %.2f,\n        \"p95_us\": %.2f,\n        \"min_us\": %.2f,\n        \"max_us\": %.2f,\n        \"stddev_us\": %.2f\n      },\n      \"ok\": %s,\n      \"error\": ",
                vm->iterations,
                vm->completed,
                vm->expected_result,
                vm->actual_result,
                vm->result_mismatch ? "true" : "false",
                vm->halt_ratio,
                vm->throughput_ops,
                vm->avg_steps,
                vm->min_steps,
                vm->max_steps,
                vm->timings.mean_us,
                vm->timings.p95_us,
                vm->timings.min_us,
                vm->timings.max_us,
                vm->timings.stddev_us,
                vm->ok ? "true" : "false");
        json_write_string(fp, vm->error);
        fprintf(fp, "\n    }%s\n", (i + 1 < report->vm_count) ? "," : "");
    }

    fprintf(fp, "  ]");

    fprintf(fp, ",\n  \"fkv\": ");
    if (!report->has_fkv) {
        fprintf(fp, "null\n");
    } else {
        const bench_fkv_report_t *fkv = &report->fkv;
        fprintf(fp,
                "{\n    \"operations\": %zu,\n    \"put_completed\": %zu,\n    \"get_completed\": %zu,\n    \"hits\": %zu,\n    \"value_mismatches\": %zu,\n    \"hit_rate\": %.4f,\n    \"ok\": %s,\n    \"error\": ",
                fkv->operations,
                fkv->put_completed,
                fkv->get_completed,
                fkv->hits,
                fkv->value_mismatches,
                fkv->hit_rate,
                fkv->ok ? "true" : "false");
        json_write_string(fp, fkv->error);
        fprintf(fp,
                ",\n    \"put\": {\n      \"timing\": {\n        \"mean_us\": %.2f,\n        \"p95_us\": %.2f,\n        \"min_us\": %.2f,\n        \"max_us\": %.2f,\n        \"stddev_us\": %.2f\n      },\n      \"throughput_ops\": %.2f\n    },\n    \"get\": {\n      \"timing\": {\n        \"mean_us\": %.2f,\n        \"p95_us\": %.2f,\n        \"min_us\": %.2f,\n        \"max_us\": %.2f,\n        \"stddev_us\": %.2f\n      },\n      \"throughput_ops\": %.2f\n    }\n  }\n",
                fkv->put_timings.mean_us,
                fkv->put_timings.p95_us,
                fkv->put_timings.min_us,
                fkv->put_timings.max_us,
                fkv->put_timings.stddev_us,
                fkv->put_throughput_ops,
                fkv->get_timings.mean_us,
                fkv->get_timings.p95_us,
                fkv->get_timings.min_us,
                fkv->get_timings.max_us,
                fkv->get_timings.stddev_us,
                fkv->get_throughput_ops);
    }

    fprintf(fp, "}\n");

    if (fclose(fp) != 0) {
        remove(tmp_path);
        return -1;
    }
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return -1;
    }
    return 0;
}

static int run_chat(const kolibri_config_t *cfg) {
    KolibriAI *ai = kolibri_ai_create(NULL);
    if (ai) {
        if (cfg) {
            KolibriAISelfplayConfig sp = {
                .tasks_per_iteration = cfg->selfplay.tasks_per_iteration,
                .max_difficulty = cfg->selfplay.max_difficulty,
            };
            kolibri_ai_set_selfplay_config(ai, &sp);
        }
        kolibri_ai_start(ai);
    }

    printf("Kolibri CLI чат. Введите арифметику или задайте вопрос. 'exit' для выхода.\n");

    char *line = NULL;
    size_t line_cap = 0;
    size_t exchange_id = 0;

    while (1) {
        printf("вы> ");
        fflush(stdout);
        ssize_t read = getline(&line, &line_cap, stdin);
        if (read < 0) {
            printf("\n");
            break;
        }
        trim_inplace(line);
        if (line[0] == '\0') {
            continue;
        }
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            printf("kolibri> До встречи!\n");
            break;
        }

        uint64_t value = 0;
        uint32_t steps = 0;
        if (try_evaluate_expression(cfg, line, &value, &steps) == 0) {
            exchange_id++;
            uint8_t key_digits[32];
            size_t key_len = 0;
            uint8_t val_digits[32];
            size_t val_len = 0;
            int stored = 0;
            if (digits_from_number(exchange_id, key_digits, sizeof(key_digits), &key_len) == 0 &&
                digits_from_number(value, val_digits, sizeof(val_digits), &val_len) == 0) {
                if (fkv_put(key_digits, key_len, val_digits, val_len, FKV_ENTRY_TYPE_VALUE) == 0) {
                    stored = 1;
                }
            }
            printf("kolibri> Ответ Δ-VM: %" PRIu64 " (шагов: %u)%s\n",
                   value,
                   steps,
                   stored ? " — записано в F-KV" : "");
            if (ai) {
                record_interaction(ai, line, 1.0, 1, (double)value);
                kolibri_ai_process_iteration(ai);
            }
            continue;
        }

        if (ai) {
            record_interaction(ai, line, 0.25, 0, 0.0);
            kolibri_ai_process_iteration(ai);
        }
        describe_best_formula(ai);
    }

    free(line);
    if (ai) {
        kolibri_ai_destroy(ai);
    }
    return 0;
}

static int run_bench(void) {
    if (mkdir("logs", 0755) != 0 && errno != EEXIST) {
        log_warn("failed to create logs directory: %s", strerror(errno));
    }

    FILE *bench_log = fopen("logs/bench.log", "a");
    if (!bench_log) {
        log_warn("could not open logs/bench.log: %s", strerror(errno));
    }

    bench_log_line(bench_log, "=== Kolibri Ω benchmark suite ===");

    int rc = 0;
    bench_report_t report;
    memset(&report, 0, sizeof(report));

    if (run_vm_microbench(bench_log, &report) != 0) {
        rc = -1;
    }
    if (run_fkv_microbench(bench_log, &report) != 0) {
        rc = -1;
    }

    bench_log_line(bench_log, "=== Benchmarks completed (%s) ===", rc == 0 ? "OK" : "FAIL");
    if (bench_log) {
        fclose(bench_log);
    }

    report.overall_ok = (rc == 0);
    if (write_bench_json("logs/bench.json", &report) != 0) {
        log_warn("failed to write logs/bench.json: %s", strerror(errno));
    } else {
        log_info("Benchmark JSON report saved to logs/bench.json");
    }
    log_info("Benchmark report saved to logs/bench.log");
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    log_set_level(LOG_LEVEL_INFO);
    FILE *log_fp = fopen("logs/kolibri.log", "a");
    if (log_fp) {
        log_set_file(log_fp);
    }

    kolibri_config_t cfg;
    if (config_load("cfg/kolibri.jsonc", &cfg) != 0) {
        log_warn("could not read cfg/kolibri.jsonc, using defaults");
    }

    if (argc > 1 && strcmp(argv[1], "--bench") == 0) {
        if (log_fp) {
            log_set_file(NULL);
            fclose(log_fp);
        }
        return run_bench();
    }

    if (argc > 1 && strcmp(argv[1], "--chat") == 0) {
        if (fkv_init() != 0) {
            log_error("failed to initialize F-KV");
            if (log_fp) {
                fclose(log_fp);
            }
            return 1;
        }
        int rc = run_chat(&cfg);
        fkv_shutdown();
        if (log_fp) {
            fclose(log_fp);
        }
        return rc;
    }

    if (fkv_init() != 0) {
        log_error("failed to initialize F-KV");
        if (log_fp) {
            fclose(log_fp);
        }
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (http_server_start(&cfg) != 0) {
        log_error("failed to start HTTP server");
        fkv_shutdown();
        if (log_fp) {
            fclose(log_fp);
        }
        return 1;
    }

    while (running) {
        pause();
    }

    http_server_stop();
    fkv_shutdown();
    if (log_fp) {
        fclose(log_fp);
    }
    return 0;
}
