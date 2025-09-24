


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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
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
                                   vm_scheduler_t *scheduler,
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

    prog_t prog = {.code = bytecode, .len = bytecode_len};
    vm_limits_t limits = {0};
    if (cfg) {
        limits.max_steps = cfg->vm.max_steps ? cfg->vm.max_steps : 256u;
        limits.max_stack = cfg->vm.max_stack ? cfg->vm.max_stack : 64u;
    } else {
        limits.max_steps = 256u;
        limits.max_stack = 64u;
    }

    vm_result_t result = {0};
    int rc = -1;

    if (scheduler) {
        vm_context_t *ctx = NULL;
        if (vm_scheduler_spawn(scheduler, &prog, &limits, 1, NULL, &result, &ctx) != 0) {
            free(bytecode);
            return -1;
        }
        while (!vm_context_finished(ctx)) {
            if (vm_scheduler_step(scheduler) != 0) {
                vm_scheduler_release(scheduler, ctx);
                free(bytecode);
                return -1;
            }
        }
        vm_status_t status = vm_context_status(ctx);
        vm_scheduler_release(scheduler, ctx);
        if (status == VM_OK) {
            rc = 0;
        }
    } else {
        if (vm_run(&prog, &limits, NULL, &result) == 0 && result.status == VM_OK) {
            rc = 0;
        }
    }

    free(bytecode);

    if (rc != 0) {
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

    vm_scheduler_t scheduler;
    if (vm_scheduler_init(&scheduler,
                          cfg ? cfg->vm.stack_pool_size : 4,
                          cfg ? cfg->vm.max_stack : 128,
                          cfg ? cfg->vm.gas_quantum : 64,
                          cfg ? cfg->vm.max_contexts : 0) != 0) {
        log_error("failed to initialize VM scheduler for CLI");
        if (ai) {
            kolibri_ai_destroy(ai);
        }
        return -1;
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
        if (try_evaluate_expression(cfg, &scheduler, line, &value, &steps) == 0) {
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
    vm_scheduler_destroy(&scheduler);
    return 0;
}

static int run_bench(void) {
    log_info("Benchmarks are not implemented yet");
    return 0;
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

    vm_scheduler_t scheduler;
    if (vm_scheduler_init(&scheduler,
                          cfg.vm.stack_pool_size,
                          cfg.vm.max_stack,
                          cfg.vm.gas_quantum,
                          cfg.vm.max_contexts) != 0) {
        log_error("failed to initialize VM scheduler");
        fkv_shutdown();
        if (log_fp) {
            fclose(log_fp);
        }
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (http_server_start(&cfg, &scheduler) != 0) {
        log_error("failed to start HTTP server");
        vm_scheduler_destroy(&scheduler);
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
    vm_scheduler_destroy(&scheduler);
    fkv_shutdown();
    if (log_fp) {
        fclose(log_fp);
    }
    return 0;
}
