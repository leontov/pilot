#define _POSIX_C_SOURCE 200809L

#include "kolibri_ai.h"

#include "formula.h"
#include "util/config.h"
#include "util/log.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct KolibriAI {
    pthread_t worker;
    int running;
    pthread_mutex_t mutex;

    FormulaCollection *library;

    double average_reward;
    double exploration_rate;
    double exploitation_rate;
    uint64_t iterations;

    KolibriAISelfplayConfig selfplay_config;
    uint32_t selfplay_current_difficulty;
    size_t selfplay_recent_success;
    size_t selfplay_recent_total;
    double selfplay_reward_avg;
    uint64_t selfplay_total_interactions;
    unsigned int rng_state;
};

typedef struct {
    const char *id;
    const char *content;
    double effectiveness;
} default_formula_t;

static const default_formula_t k_default_formulas[] = {
    {"kolibri.arith.decimal", "f(x, y) = x + y", 0.62},
    {"kolibri.memory.recall", "remember(city) -> answer(city)", 0.58},
    {"kolibri.pattern.sequence", "g(n) = 2*n + 1", 0.64},
};

static double kolibri_ai_reduce_task(const KolibriSelfplayTask *task) {
    if (!task || task->operand_count == 0) {
        return 0.0;
    }

    double value = task->operands[0];
    for (size_t i = 0; i < task->operator_count && i + 1 < task->operand_count; ++i) {
        char op = task->operators[i];
        double rhs = task->operands[i + 1];
        switch (op) {
        case '+':
            value += rhs;
            break;
        case '-':
            value -= rhs;
            break;
        case '*':
            value *= rhs;
            break;
        default:
            value += rhs;
            break;
        }
    }
    return value;
}

static double kolibri_ai_apply_formula_to_task(const Formula *formula,
                                               const KolibriSelfplayTask *task,
                                               int *handled) {
    if (handled) {
        *handled = 0;
    }
    if (!formula || !task || formula->representation != FORMULA_REPRESENTATION_TEXT) {
        return 0.0;
    }

    if (strstr(formula->content, "x + y") != NULL) {
        for (size_t i = 0; i < task->operator_count; ++i) {
            if (task->operators[i] != '+') {
                return 0.0;
            }
        }
        if (handled) {
            *handled = 1;
        }
        return kolibri_ai_reduce_task(task);
    }

    if (strstr(formula->content, "x - y") != NULL) {
        for (size_t i = 0; i < task->operator_count; ++i) {
            if (task->operators[i] != '-') {
                return 0.0;
            }
        }
        if (handled) {
            *handled = 1;
        }
        return kolibri_ai_reduce_task(task);
    }

    if (strstr(formula->content, "x * y") != NULL) {
        for (size_t i = 0; i < task->operator_count; ++i) {
            if (task->operators[i] != '*') {
                return 0.0;
            }
        }
        if (handled) {
            *handled = 1;
        }
        return kolibri_ai_reduce_task(task);
    }

    return 0.0;
}

static double kolibri_ai_predict_on_task(KolibriAI *ai,
                                         const KolibriSelfplayTask *task,
                                         int *handled) {
    if (handled) {
        *handled = 0;
    }
    const Formula *candidates[3] = {0};
    size_t count = (ai && ai->library) ? formula_collection_get_top(ai->library, candidates, 3) : 0;
    for (size_t i = 0; i < count; ++i) {
        int local_handled = 0;
        double value = kolibri_ai_apply_formula_to_task(candidates[i], task, &local_handled);
        if (local_handled) {
            if (handled) {
                *handled = 1;
            }
            return value;
        }
    }

    double baseline = kolibri_ai_reduce_task(task);
    if (!ai) {
        return baseline;
    }
    unsigned int noise_raw = rand_r(&ai->rng_state);
    double magnitude = fabs(baseline);
    if (magnitude < 1.0) {
        magnitude = 1.0;
    }
    double jitter = ((int)(noise_raw % 201) - 100) / 1000.0; // [-0.1, 0.1]
    return baseline + magnitude * jitter * 0.05;
}

static void kolibri_ai_seed_library(KolibriAI *ai) {
    if (!ai || !ai->library) {
        return;
    }

    time_t now = time(NULL);
    for (size_t i = 0; i < sizeof(k_default_formulas) / sizeof(k_default_formulas[0]); ++i) {
        Formula formula;
        memset(&formula, 0, sizeof(formula));
        formula.representation = FORMULA_REPRESENTATION_TEXT;
        strncpy(formula.id, k_default_formulas[i].id, sizeof(formula.id) - 1);
        strncpy(formula.content, k_default_formulas[i].content, sizeof(formula.content) - 1);
        formula.effectiveness = k_default_formulas[i].effectiveness;
        formula.created_at = now - (time_t)((sizeof(k_default_formulas) - i) * 90);
        formula.tests_passed = 1;
        formula.confirmations = 1;
        formula_collection_add(ai->library, &formula);
    }

    ai->average_reward = 0.0;
    ai->exploitation_rate = 0.65;
    ai->exploration_rate = 0.35;

    if (ai->library->count > 0) {
        double total = 0.0;
        for (size_t i = 0; i < ai->library->count; ++i) {
            total += ai->library->formulas[i].effectiveness;
        }
        ai->average_reward = total / (double)ai->library->count;
    }
}

static void kolibri_ai_synthesise_formula(KolibriAI *ai) {
    if (!ai || !ai->library) {
        return;
    }

    const size_t max_synthesized = 32;
    if (ai->library->count >= max_synthesized) {
        return;
    }

    double phase = sin((double)ai->iterations / 30.0);
    double effectiveness = 0.55 + 0.15 * phase;

    Formula formula;
    memset(&formula, 0, sizeof(formula));
    formula.representation = FORMULA_REPRESENTATION_TEXT;
    snprintf(formula.id, sizeof(formula.id), "kolibri.synthetic.%zu", ai->library->count + 1);
    snprintf(formula.content, sizeof(formula.content),
             "h_%llu(x) = %.0fx + %.0f", (unsigned long long)ai->iterations,
             round(phase * 5.0) + 2.0, round(phase * 3.0) + 1.0);
    formula.effectiveness = effectiveness;
    formula.created_at = time(NULL);
    formula.tests_passed = 1;

    formula_collection_add(ai->library, &formula);

    if (ai->library->count > 0) {
        double total = 0.0;
        for (size_t i = 0; i < ai->library->count; ++i) {
            total += ai->library->formulas[i].effectiveness;
        }
        ai->average_reward = total / (double)ai->library->count;
    }
}

KolibriAI *kolibri_ai_create(void) {
    KolibriAI *ai = calloc(1, sizeof(KolibriAI));
    if (!ai) {
        return NULL;
    }

    if (pthread_mutex_init(&ai->mutex, NULL) != 0) {
        free(ai);
        return NULL;
    }

    ai->library = formula_collection_create(8);
    if (!ai->library) {
        pthread_mutex_destroy(&ai->mutex);
        free(ai);
        return NULL;
    }

    ai->iterations = 0;
    ai->average_reward = 0.0;
    ai->exploration_rate = 0.4;
    ai->exploitation_rate = 0.6;
    ai->selfplay_config.tasks_per_iteration = 2;
    ai->selfplay_config.max_difficulty = 3;
    ai->selfplay_current_difficulty = 1;
    ai->selfplay_recent_success = 0;
    ai->selfplay_recent_total = 0;
    ai->selfplay_reward_avg = 0.0;
    ai->selfplay_total_interactions = 0;
    ai->rng_state = (unsigned int)time(NULL);
    if (ai->rng_state == 0) {
        ai->rng_state = 1;
    }

    kolibri_ai_seed_library(ai);
    return ai;
}

void kolibri_ai_destroy(KolibriAI *ai) {
    if (!ai) {
        return;
    }

    kolibri_ai_stop(ai);
    pthread_mutex_destroy(&ai->mutex);
    if (ai->library) {
        formula_collection_destroy(ai->library);
    }
    free(ai);
}

static void *kolibri_ai_worker(void *arg) {
    KolibriAI *ai = (KolibriAI *)arg;
    while (1) {
        pthread_mutex_lock(&ai->mutex);
        int should_continue = ai->running;
        pthread_mutex_unlock(&ai->mutex);

        if (!should_continue) {
            break;
        }

        kolibri_ai_process_iteration(ai);
        struct timespec req = {0, 75000 * 1000};
        nanosleep(&req, NULL);
    }
    return NULL;
}

void kolibri_ai_start(KolibriAI *ai) {
    if (!ai) {
        return;
    }

    pthread_mutex_lock(&ai->mutex);
    if (ai->running) {
        pthread_mutex_unlock(&ai->mutex);
        return;
    }
    ai->running = 1;
    pthread_mutex_unlock(&ai->mutex);

    pthread_create(&ai->worker, NULL, kolibri_ai_worker, ai);
}

void kolibri_ai_stop(KolibriAI *ai) {
    if (!ai) {
        return;
    }

    pthread_mutex_lock(&ai->mutex);
    if (!ai->running) {
        pthread_mutex_unlock(&ai->mutex);
        return;
    }
    ai->running = 0;
    pthread_mutex_unlock(&ai->mutex);

    pthread_join(ai->worker, NULL);
}

void kolibri_ai_set_selfplay_config(KolibriAI *ai, const KolibriAISelfplayConfig *config) {
    if (!ai || !config) {
        return;
    }

    pthread_mutex_lock(&ai->mutex);
    ai->selfplay_config.tasks_per_iteration = config->tasks_per_iteration;
    uint32_t max_difficulty = config->max_difficulty;
    if (max_difficulty == 0) {
        max_difficulty = 1;
    }
    ai->selfplay_config.max_difficulty = max_difficulty;
    if (ai->selfplay_current_difficulty == 0) {
        ai->selfplay_current_difficulty = 1;
    }
    if (ai->selfplay_current_difficulty > ai->selfplay_config.max_difficulty) {
        ai->selfplay_current_difficulty = ai->selfplay_config.max_difficulty;
    }
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_record_interaction(KolibriAI *ai, const KolibriAISelfplayInteraction *interaction) {
    if (!ai || !interaction) {
        return;
    }

    log_info("self-play: %s | expected=%.3f predicted=%.3f error=%.3f reward=%.3f success=%s diff=%u",
             interaction->task.description,
             interaction->task.expected_result,
             interaction->predicted_result,
             interaction->error,
             interaction->reward,
             interaction->success ? "yes" : "no",
             interaction->task.difficulty);

    ai->selfplay_total_interactions++;
    double count = (double)ai->selfplay_total_interactions;
    if (count <= 0.0) {
        count = 1.0;
    }
    ai->selfplay_reward_avg += (interaction->reward - ai->selfplay_reward_avg) / count;

    ai->selfplay_recent_total++;
    if (interaction->success) {
        ai->selfplay_recent_success++;
    }

    size_t window = ai->selfplay_config.tasks_per_iteration;
    if (window == 0) {
        window = 1;
    }
    if (ai->selfplay_recent_total >= window) {
        double ratio = (double)ai->selfplay_recent_success;
        ratio /= (double)ai->selfplay_recent_total;
        if (ratio > 0.75 && ai->selfplay_current_difficulty < ai->selfplay_config.max_difficulty) {
            ai->selfplay_current_difficulty++;
        } else if (ratio < 0.35 && ai->selfplay_current_difficulty > 1) {
            ai->selfplay_current_difficulty--;
        }
        ai->selfplay_recent_total = 0;
        ai->selfplay_recent_success = 0;
    }
}

void kolibri_ai_apply_config(KolibriAI *ai, const struct kolibri_config_t *cfg) {
    if (!ai || !cfg) {
        return;
    }

    KolibriAISelfplayConfig sp_config = {
        .tasks_per_iteration = cfg->selfplay.tasks_per_iteration,
        .max_difficulty = cfg->selfplay.max_difficulty,
    };
    kolibri_ai_set_selfplay_config(ai, &sp_config);

    pthread_mutex_lock(&ai->mutex);
    if (cfg->seed != 0) {
        ai->rng_state = cfg->seed;
    }
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_process_iteration(KolibriAI *ai) {
    if (!ai) {
        return;
    }

    pthread_mutex_lock(&ai->mutex);

    ai->iterations++;

    double phase = sin((double)ai->iterations / 24.0);
    double exploitation_delta = 0.02 * phase;
    double exploration_delta = 0.015 * cos((double)ai->iterations / 18.0);

    ai->exploitation_rate = fmin(0.9, fmax(0.5, ai->exploitation_rate + exploitation_delta));
    ai->exploration_rate = fmin(0.5, fmax(0.1, ai->exploration_rate + exploration_delta));

    size_t tasks_to_run = ai->selfplay_config.tasks_per_iteration;
    if (tasks_to_run > 0) {
        uint32_t difficulty_cap = ai->selfplay_config.max_difficulty;
        if (difficulty_cap == 0) {
            difficulty_cap = 1;
        }
        uint32_t target_difficulty = ai->selfplay_current_difficulty;
        if (target_difficulty == 0) {
            target_difficulty = 1;
        }
        if (target_difficulty > difficulty_cap) {
            target_difficulty = difficulty_cap;
        }

        for (size_t i = 0; i < tasks_to_run; ++i) {
            KolibriSelfplayTask task;
            if (kolibri_selfplay_generate_task(&ai->rng_state, target_difficulty, &task) != 0) {
                continue;
            }

            double predicted = kolibri_ai_predict_on_task(ai, &task, NULL);
            double error = fabs(predicted - task.expected_result);
            int success = (error < 0.25);
            double reward = success ? 1.0 - fmin(error, 1.0) : -fmin(error, 1.0);

            KolibriAISelfplayInteraction interaction;
            interaction.task = task;
            interaction.predicted_result = predicted;
            interaction.error = error;
            interaction.reward = reward;
            interaction.success = success;

            kolibri_ai_record_interaction(ai, &interaction);
        }

        if (ai->selfplay_total_interactions > 0) {
            double blend = ai->selfplay_reward_avg;
            ai->average_reward = 0.8 * ai->average_reward + 0.2 * blend;
        }
    }

    if (ai->iterations % 60 == 0) {
        kolibri_ai_synthesise_formula(ai);
    }

    pthread_mutex_unlock(&ai->mutex);
}

int kolibri_ai_add_formula(KolibriAI *ai, const Formula *formula) {
    if (!ai || !formula) {
        return -1;
    }

    pthread_mutex_lock(&ai->mutex);
    int rc = formula_collection_add(ai->library, formula);
    if (rc == 0 && ai->library->count > 0) {
        double total = 0.0;
        for (size_t i = 0; i < ai->library->count; ++i) {
            total += ai->library->formulas[i].effectiveness;
        }
        ai->average_reward = total / (double)ai->library->count;
    }
    pthread_mutex_unlock(&ai->mutex);
    return rc;
}

Formula *kolibri_ai_get_best_formula(KolibriAI *ai) {
    if (!ai || !ai->library) {
        return NULL;
    }

    pthread_mutex_lock(&ai->mutex);
    const Formula *top[1] = {0};
    Formula *copy = NULL;
    if (formula_collection_get_top(ai->library, top, 1) == 1) {
        copy = calloc(1, sizeof(Formula));
        if (copy && formula_copy(copy, top[0]) != 0) {
            free(copy);
            copy = NULL;
        }
    }
    pthread_mutex_unlock(&ai->mutex);
    return copy;
}

static char *kolibri_ai_alloc_json(size_t initial) {
    char *buffer = malloc(initial);
    if (buffer) {
        buffer[0] = '\0';
    }
    return buffer;
}

char *kolibri_ai_serialize_state(const KolibriAI *ai) {
    if (!ai) {
        return NULL;
    }

    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    uint64_t iterations = ai->iterations;
    size_t formula_count = ai->library ? ai->library->count : 0;
    double avg_reward = ai->average_reward;
    double exploitation = ai->exploitation_rate;
    double exploration = ai->exploration_rate;
    int running = ai->running;
    uint32_t difficulty = ai->selfplay_current_difficulty;
    double selfplay_reward = ai->selfplay_reward_avg;
    uint32_t tasks_per_iteration = ai->selfplay_config.tasks_per_iteration;
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    char temp[256];
    int written = snprintf(temp, sizeof(temp),
                           "{\"iterations\":%llu,\"formula_count\":%zu,\"average_reward\":%.3f,"
                           "\"exploitation_rate\":%.3f,\"exploration_rate\":%.3f,\"running\":%d,"
                           "\"selfplay_difficulty\":%u,\"selfplay_reward\":%.3f,\"selfplay_tasks\":%u}",
                           (unsigned long long)iterations,
                           formula_count,
                           avg_reward,
                           exploitation,
                           exploration,
                           running,
                           difficulty,
                           selfplay_reward,
                           tasks_per_iteration);
    if (written < 0) {
        return NULL;
    }
    char *json = malloc((size_t)written + 1);
    if (!json) {
        return NULL;
    }
    memcpy(json, temp, (size_t)written + 1);
    return json;
}

char *kolibri_ai_serialize_formulas(const KolibriAI *ai, size_t max_results) {
    if (!ai || max_results == 0) {
        return NULL;
    }

    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    const Formula *top[16] = {0};
    if (max_results > sizeof(top) / sizeof(top[0])) {
        max_results = sizeof(top) / sizeof(top[0]);
    }
    size_t count = ai->library ? formula_collection_get_top(ai->library, top, max_results) : 0;
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    size_t capacity = 256;
    char *json = kolibri_ai_alloc_json(capacity);
    if (!json) {
        return NULL;
    }

    size_t len = 0;
    int needed = snprintf(json, capacity, "{\"formulas\":[");
    if (needed < 0) {
        free(json);
        return NULL;
    }
    len = (size_t)needed;

    for (size_t i = 0; i < count; ++i) {
        const Formula *formula = top[i];
        if (!formula) {
            continue;
        }

        char iso_time[32];
        struct tm tm_buf;
        time_t created = formula->created_at;
        gmtime_r(&created, &tm_buf);
        strftime(iso_time, sizeof(iso_time), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);

        char entry[512];
        needed = snprintf(entry, sizeof(entry),
                          "%s{\"id\":\"%s\",\"content\":\"%s\",\"effectiveness\":%.3f,\"created_at\":\"%s\"}",
                          (i == 0) ? "" : ",",
                          formula->id,
                          formula->content,
                          formula->effectiveness,
                          iso_time);
        if (needed < 0) {
            free(json);
            return NULL;
        }

        if (len + (size_t)needed + 2 > capacity) {
            size_t new_capacity = capacity;
            while (len + (size_t)needed + 2 > new_capacity) {
                new_capacity *= 2;
            }
            char *tmp = realloc(json, new_capacity);
            if (!tmp) {
                free(json);
                return NULL;
            }
            json = tmp;
            capacity = new_capacity;
        }

        memcpy(json + len, entry, (size_t)needed);
        len += (size_t)needed;
        json[len] = '\0';
    }

    if (len + 2 > capacity) {
        char *tmp = realloc(json, len + 2);
        if (!tmp) {
            free(json);
            return NULL;
        }
        json = tmp;
        capacity = len + 2;
    }

    snprintf(json + len, capacity - len, "]}");
    return json;
}
