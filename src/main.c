


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

typedef struct {
    const char *name;
    size_t iterations;
    size_t warmup;
    double mean_us;
    double p95_us;
    double min_us;
    double max_us;
    double stddev_us;
    double throughput_ops;
    uint32_t steps;
    int has_steps;
} bench_stats_t;

static double elapsed_us(const struct timespec *start, const struct timespec *end) {
    time_t sec = end->tv_sec - start->tv_sec;
    long nsec = end->tv_nsec - start->tv_nsec;
    if (nsec < 0) {
        sec -= 1;
        nsec += 1000000000L;
    }
    return (double)sec * 1e6 + (double)nsec / 1e3;
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

static double percentile(double *values, size_t n, double p) {
    if (n == 0) {
        return 0.0;
    }
    if (n == 1) {
        return values[0];
    }
    if (p <= 0.0) {
        return values[0];
    }
    if (p >= 100.0) {
        return values[n - 1];
    }
    double rank = (p / 100.0) * (double)(n - 1);
    size_t lower = (size_t)rank;
    size_t upper = lower + 1;
    if (upper >= n) {
        return values[n - 1];
    }
    double fraction = rank - (double)lower;
    return values[lower] + fraction * (values[upper] - values[lower]);
}

static int bench_vm_sum_digits(size_t iterations, size_t warmup, bench_stats_t *out) {
    if (!out || iterations == 0) {
        return -1;
    }

    static const uint8_t program[] = {
        0x01, 0x00, // PUSHd 0
        0x01, 0x01, // PUSHd 1
        0x02,       // ADD10
        0x01, 0x02, // PUSHd 2
        0x02,       // ADD10
        0x01, 0x03, // PUSHd 3
        0x02,       // ADD10
        0x01, 0x04, // PUSHd 4
        0x02,       // ADD10
        0x01, 0x05, // PUSHd 5
        0x02,       // ADD10
        0x01, 0x06, // PUSHd 6
        0x02,       // ADD10
        0x01, 0x07, // PUSHd 7
        0x02,       // ADD10
        0x01, 0x08, // PUSHd 8
        0x02,       // ADD10
        0x01, 0x09, // PUSHd 9
        0x02,       // ADD10
        0x12        // HALT
    };

    double *samples = calloc(iterations, sizeof(double));
    if (!samples) {
        return -1;
    }

    vm_limits_t limits = {.max_steps = 256, .max_stack = 64};
    prog_t prog = {.code = program, .len = sizeof(program)};
    double sum = 0.0;
    double sum_sq = 0.0;
    double min_us = 0.0;
    double max_us = 0.0;
    uint32_t last_steps = 0;

    size_t total = warmup + iterations;
    size_t sample_index = 0;
    for (size_t i = 0; i < total; ++i) {
        struct timespec start_ts;
        struct timespec end_ts;
        if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
            free(samples);
            return -1;
        }

        vm_result_t result = {0};
        if (vm_run(&prog, &limits, NULL, &result) != 0 || result.status != VM_OK || !result.halted) {
            free(samples);
            return -1;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &end_ts) != 0) {
            free(samples);
            return -1;
        }

        double elapsed = elapsed_us(&start_ts, &end_ts);
        if (i >= warmup) {
            samples[sample_index++] = elapsed;
            sum += elapsed;
            sum_sq += elapsed * elapsed;
            if (sample_index == 1) {
                min_us = max_us = elapsed;
            } else {
                if (elapsed < min_us) {
                    min_us = elapsed;
                }
                if (elapsed > max_us) {
                    max_us = elapsed;
                }
            }
            last_steps = result.steps;
        }
    }

    qsort(samples, iterations, sizeof(double), cmp_double);

    out->name = "Δ-VM sum digits";
    out->iterations = iterations;
    out->warmup = warmup;
    out->mean_us = sum / (double)iterations;
    out->p95_us = percentile(samples, iterations, 95.0);
    out->min_us = min_us;
    out->max_us = max_us;
    double mean = out->mean_us;
    double variance = 0.0;
    if (iterations > 1) {
        variance = (sum_sq - (sum * sum) / (double)iterations) / (double)(iterations - 1);
    }
    out->stddev_us = variance > 0.0 ? sqrt(variance) : 0.0;
    out->throughput_ops = mean > 0.0 ? 1e6 / mean : 0.0;
    out->steps = last_steps;
    out->has_steps = 1;

    free(samples);
    return 0;
}

static int bench_fkv_put(size_t iterations, size_t warmup, bench_stats_t *out) {
    if (!out || iterations == 0) {
        return -1;
    }

    double *samples = calloc(iterations, sizeof(double));
    if (!samples) {
        return -1;
    }

    double sum = 0.0;
    double sum_sq = 0.0;
    double min_us = 0.0;
    double max_us = 0.0;

    size_t total = warmup + iterations;
    size_t sample_index = 0;
    for (size_t i = 0; i < total; ++i) {
        uint8_t key_digits[32];
        uint8_t value_digits[32];
        size_t key_len = 0;
        size_t value_len = 0;
        uint64_t key_index = (uint64_t)(i + 1);
        uint64_t value_index = (uint64_t)(i * 3 + 7);
        if (digits_from_number(key_index, key_digits, sizeof(key_digits), &key_len) != 0 ||
            digits_from_number(value_index, value_digits, sizeof(value_digits), &value_len) != 0) {
            free(samples);
            return -1;
        }

        struct timespec start_ts;
        struct timespec end_ts;
        if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
            free(samples);
            return -1;
        }

        if (fkv_put(key_digits, key_len, value_digits, value_len, FKV_ENTRY_TYPE_VALUE) != 0) {
            free(samples);
            return -1;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &end_ts) != 0) {
            free(samples);
            return -1;
        }

        double elapsed = elapsed_us(&start_ts, &end_ts);
        if (i >= warmup) {
            samples[sample_index++] = elapsed;
            sum += elapsed;
            sum_sq += elapsed * elapsed;
            if (sample_index == 1) {
                min_us = max_us = elapsed;
            } else {
                if (elapsed < min_us) {
                    min_us = elapsed;
                }
                if (elapsed > max_us) {
                    max_us = elapsed;
                }
            }
        }
    }

    qsort(samples, iterations, sizeof(double), cmp_double);

    out->name = "F-KV put";
    out->iterations = iterations;
    out->warmup = warmup;
    out->mean_us = sum / (double)iterations;
    out->p95_us = percentile(samples, iterations, 95.0);
    out->min_us = min_us;
    out->max_us = max_us;
    double variance = 0.0;
    if (iterations > 1) {
        variance = (sum_sq - (sum * sum) / (double)iterations) / (double)(iterations - 1);
    }
    out->stddev_us = variance > 0.0 ? sqrt(variance) : 0.0;
    out->throughput_ops = out->mean_us > 0.0 ? 1e6 / out->mean_us : 0.0;
    out->steps = 0;
    out->has_steps = 0;

    free(samples);
    return 0;
}

static int bench_fkv_get(size_t iterations, size_t warmup, bench_stats_t *out) {
    if (!out || iterations == 0) {
        return -1;
    }

    double *samples = calloc(iterations, sizeof(double));
    if (!samples) {
        return -1;
    }

    double sum = 0.0;
    double sum_sq = 0.0;
    double min_us = 0.0;
    double max_us = 0.0;
    size_t total = warmup + iterations;
    size_t sample_index = 0;
    for (size_t i = 0; i < total; ++i) {
        uint8_t key_digits[32];
        size_t key_len = 0;
        size_t dataset = total > 0 ? total : iterations;
        size_t key_id = (i % dataset) + 1;
        if (digits_from_number((uint64_t)key_id, key_digits, sizeof(key_digits), &key_len) != 0) {
            free(samples);
            return -1;
        }

        struct timespec start_ts;
        struct timespec end_ts;
        if (clock_gettime(CLOCK_MONOTONIC, &start_ts) != 0) {
            free(samples);
            return -1;
        }

        fkv_iter_t it;
        if (fkv_get_prefix(key_digits, key_len, &it, 4) != 0) {
            free(samples);
            return -1;
        }

        if (clock_gettime(CLOCK_MONOTONIC, &end_ts) != 0) {
            fkv_iter_free(&it);
            free(samples);
            return -1;
        }

        double elapsed = elapsed_us(&start_ts, &end_ts);
        if (i >= warmup) {
            samples[sample_index++] = elapsed;
            sum += elapsed;
            sum_sq += elapsed * elapsed;
            if (sample_index == 1) {
                min_us = max_us = elapsed;
            } else {
                if (elapsed < min_us) {
                    min_us = elapsed;
                }
                if (elapsed > max_us) {
                    max_us = elapsed;
                }
            }
        }

        fkv_iter_free(&it);
    }

    qsort(samples, iterations, sizeof(double), cmp_double);

    out->name = "F-KV get_prefix";
    out->iterations = iterations;
    out->warmup = warmup;
    out->mean_us = sum / (double)iterations;
    out->p95_us = percentile(samples, iterations, 95.0);
    out->min_us = min_us;
    out->max_us = max_us;
    double variance = 0.0;
    if (iterations > 1) {
        variance = (sum_sq - (sum * sum) / (double)iterations) / (double)(iterations - 1);
    }
    out->stddev_us = variance > 0.0 ? sqrt(variance) : 0.0;
    out->throughput_ops = out->mean_us > 0.0 ? 1e6 / out->mean_us : 0.0;
    out->steps = 0;
    out->has_steps = 0;

    free(samples);
    return 0;
}

static void print_bench_report(const bench_stats_t *results, size_t count) {
    if (!results || count == 0) {
        return;
    }
    puts("=== Kolibri Microbenchmarks ===");
    printf("%-24s %8s %7s %11s %11s %11s %11s %11s %11s %8s\n",
           "Benchmark",
           "Iter",
           "Warmup",
           "Mean (us)",
           "P95 (us)",
           "StdDev",
           "Min",
           "Max",
           "Ops/s",
           "Steps");
    puts("-----------------------------------------------------------------------------------------------------------------");
    for (size_t i = 0; i < count; ++i) {
        char steps_buf[32];
        if (results[i].has_steps) {
            snprintf(steps_buf, sizeof(steps_buf), "%" PRIu32, results[i].steps);
        } else {
            snprintf(steps_buf, sizeof(steps_buf), "-");
        }
        printf("%-24s %8zu %7zu %11.2f %11.2f %11.2f %11.2f %11.2f %11.0f %8s\n",
               results[i].name,
               results[i].iterations,
               results[i].warmup,
               results[i].mean_us,
               results[i].p95_us,
               results[i].stddev_us,
               results[i].min_us,
               results[i].max_us,
               results[i].throughput_ops,
               steps_buf);
    }
}

static void write_bench_json(const bench_stats_t *results,
                             size_t count,
                             size_t iterations,
                             size_t warmup,
                             const char *path) {
    if (!results || count == 0 || !path) {
        return;
    }

    FILE *fp = fopen(path, "w");
    if (!fp) {
        log_warn("could not open %s for writing", path);
        return;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);
    char timestamp[64];
    if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", &tm_info) == 0) {
        timestamp[0] = '\0';
    }

    fprintf(fp,
            "{\n  \"timestamp\": \"%s\",\n  \"iterations\": %zu,\n  \"warmup\": %zu,\n  \"results\": [\n",
            timestamp,
            iterations,
            warmup);
    for (size_t i = 0; i < count; ++i) {
        const bench_stats_t *stat = &results[i];
        fprintf(fp,
                "    {\"name\": \"%s\", \"iterations\": %zu, \"warmup\": %zu, \"mean_us\": %.4f, "
                "\"p95_us\": %.4f, \"stddev_us\": %.4f, \"min_us\": %.4f, \"max_us\": %.4f, "
                "\"throughput_ops\": %.4f, \"steps\": %u, \"has_steps\": %d}%s\n",
                stat->name,
                stat->iterations,
                stat->warmup,
                stat->mean_us,
                stat->p95_us,
                stat->stddev_us,
                stat->min_us,
                stat->max_us,
                stat->throughput_ops,
                stat->steps,
                stat->has_steps,
                (i + 1 == count) ? "" : ",");
    }
    fputs("  ]\n}\n", fp);
    fclose(fp);
}

static int run_bench(void) {
    mkdir("logs", 0755);
    log_set_file(NULL);
    FILE *log_fp = fopen("logs/kolibri.log", "a");
    if (log_fp) {
        log_set_file(log_fp);
    }

    log_info("Starting benchmark suite");

    int rc = 0;
    int fkv_ready = 0;
    if (fkv_init() != 0) {
        log_error("failed to initialize F-KV for benchmarks");
        if (log_fp) {
            fclose(log_fp);
            log_set_file(NULL);
        }
        return 1;
    }
    fkv_ready = 1;

    size_t iterations = 2000;
    const char *iter_env = getenv("KOLIBRI_BENCH_ITERATIONS");
    if (iter_env && iter_env[0]) {
        char *endptr = NULL;
        unsigned long long value = strtoull(iter_env, &endptr, 10);
        if (endptr && *endptr == '\0' && value > 0ull && value <= 10000000ull) {
            iterations = (size_t)value;
        } else {
            log_warn("invalid KOLIBRI_BENCH_ITERATIONS=%s, using default %zu", iter_env, iterations);
        }
    }

    size_t warmup = iterations / 10;
    if (warmup == 0 && iterations > 1) {
        warmup = 1;
    }
    const char *warm_env = getenv("KOLIBRI_BENCH_WARMUP");
    if (warm_env && warm_env[0]) {
        char *endptr = NULL;
        unsigned long long value = strtoull(warm_env, &endptr, 10);
        if (endptr && *endptr == '\0' && value <= 5000000ull) {
            size_t parsed = (size_t)value;
            if (parsed >= iterations) {
                log_warn("warmup %zu is not less than iterations %zu, reducing warmup", parsed, iterations);
                warmup = iterations > 1 ? iterations - 1 : 0;
            } else {
                warmup = parsed;
            }
        } else {
            log_warn("invalid KOLIBRI_BENCH_WARMUP=%s, using default %zu", warm_env, warmup);
        }
    }

    log_info("Benchmark parameters: iterations=%zu warmup=%zu", iterations, warmup);

    bench_stats_t results[3];
    size_t count = 0;

    if (bench_vm_sum_digits(iterations, warmup, &results[count]) != 0) {
        log_error("Δ-VM benchmark failed");
        rc = 1;
        goto done;
    }
    log_info("Δ-VM sum digits: mean=%.2fus p95=%.2fus std=%.2fus min=%.2fus max=%.2fus ops/s=%.0f steps=%" PRIu32,
             results[count].mean_us,
             results[count].p95_us,
             results[count].stddev_us,
             results[count].min_us,
             results[count].max_us,
             results[count].throughput_ops,
             results[count].steps);
    count++;

    if (bench_fkv_put(iterations, warmup, &results[count]) != 0) {
        log_error("F-KV put benchmark failed");
        rc = 1;
        goto done;
    }
    log_info("F-KV put: mean=%.2fus p95=%.2fus std=%.2fus min=%.2fus max=%.2fus ops/s=%.0f",
             results[count].mean_us,
             results[count].p95_us,
             results[count].stddev_us,
             results[count].min_us,
             results[count].max_us,
             results[count].throughput_ops);
    count++;

    if (bench_fkv_get(iterations, warmup, &results[count]) != 0) {
        log_error("F-KV get benchmark failed");
        rc = 1;
        goto done;
    }
    log_info("F-KV get_prefix: mean=%.2fus p95=%.2fus std=%.2fus min=%.2fus max=%.2fus ops/s=%.0f",
             results[count].mean_us,
             results[count].p95_us,
             results[count].stddev_us,
             results[count].min_us,
             results[count].max_us,
             results[count].throughput_ops);
    count++;

    print_bench_report(results, count);
    write_bench_json(results, count, iterations, warmup, "logs/bench.json");

done:
    if (fkv_ready) {
        fkv_shutdown();
    }
    if (log_fp) {
        fclose(log_fp);
        log_set_file(NULL);
    }
    return rc;
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
