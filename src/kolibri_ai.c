#include "kolibri_ai.h"

#include "formula.h"
#include "util/config.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct KolibriAI {
    FormulaCollection *library;
    double average_reward;
    double exploration_rate;
    double exploitation_rate;
    size_t iterations;
    int running;
    pthread_mutex_t mutex;
};

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

static void recompute_average_reward(KolibriAI *ai) {
    if (!ai || !ai->library || ai->library->count == 0) {
        ai->average_reward = 0.0;
        return;
    }
    double total = 0.0;
    for (size_t i = 0; i < ai->library->count; ++i) {
        total += ai->library->formulas[i].effectiveness;
    }
    ai->average_reward = total / (double)ai->library->count;
}

KolibriAI *kolibri_ai_create_with_config(const kolibri_config_t *cfg) {
    KolibriAI *ai = calloc(1, sizeof(KolibriAI));
    if (!ai) {
        return NULL;
    }
    ai->library = formula_collection_create(8);
    if (!ai->library) {
        free(ai);
        return NULL;
    }
    ai->average_reward = 0.0;
    ai->exploration_rate = 0.35;
    ai->exploitation_rate = 0.65;
    ai->iterations = 0;
    ai->running = 0;
    pthread_mutex_init(&ai->mutex, NULL);

    if (cfg) {
        kolibri_ai_apply_config(ai, cfg);
    }
    return ai;
}

KolibriAI *kolibri_ai_create(void) {
    return kolibri_ai_create_with_config(NULL);
}

void kolibri_ai_destroy(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    pthread_mutex_destroy(&ai->mutex);
    if (ai->library) {
        formula_collection_destroy(ai->library);
    }
    free(ai);
}

void kolibri_ai_start(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    ai->running = 1;
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_stop(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    ai->running = 0;
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_process_iteration(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    ai->iterations++;

    double phase = (double)ai->iterations / 24.0;
    double oscillation = 0.05 * sin(phase * M_PI * 2.0);
    ai->exploration_rate = clamp01(ai->exploration_rate + oscillation);
    ai->exploitation_rate = clamp01(1.0 - ai->exploration_rate);
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_set_selfplay_config(KolibriAI *ai, const KolibriAISelfplayConfig *config) {
    if (!ai || !config) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    double factor = config->tasks_per_iteration > 0 ? (double)config->tasks_per_iteration : 1.0;
    double normalised = clamp01(factor / 16.0);
    ai->exploration_rate = clamp01(0.25 + normalised * 0.5);
    ai->exploitation_rate = clamp01(1.0 - ai->exploration_rate);
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_record_interaction(KolibriAI *ai, const KolibriAISelfplayInteraction *interaction) {
    if (!ai || !interaction) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    double delta = interaction->reward * 0.1;
    ai->average_reward = clamp01(ai->average_reward + delta);
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_apply_config(KolibriAI *ai, const kolibri_config_t *cfg) {
    if (!ai || !cfg) {
        return;
    }
    KolibriAISelfplayConfig sp = {
        .tasks_per_iteration = cfg->selfplay.tasks_per_iteration,
        .max_difficulty = cfg->selfplay.max_difficulty,
    };
    kolibri_ai_set_selfplay_config(ai, &sp);
    pthread_mutex_lock(&ai->mutex);
    if (cfg->search.base_effectiveness > 0.0) {
        ai->average_reward = clamp01(cfg->search.base_effectiveness);
    }
    pthread_mutex_unlock(&ai->mutex);
}

KolibriDifficultyLevel kolibri_ai_plan_actions(KolibriAI *ai, double *expected_reward) {
    if (!ai) {
        if (expected_reward) {
            *expected_reward = 0.0;
        }
        return KOLIBRI_DIFFICULTY_FOUNDATION;
    }
    pthread_mutex_lock(&ai->mutex);
    double reward = ai->average_reward;
    pthread_mutex_unlock(&ai->mutex);
    if (expected_reward) {
        *expected_reward = reward;
    }
    if (reward < 0.33) {
        return KOLIBRI_DIFFICULTY_FOUNDATION;
    }
    if (reward < 0.66) {
        return KOLIBRI_DIFFICULTY_SKILLS;
    }
    if (reward < 0.85) {
        return KOLIBRI_DIFFICULTY_ADVANCED;
    }
    return KOLIBRI_DIFFICULTY_CHALLENGE;
}

int kolibri_ai_apply_reinforcement(KolibriAI *ai,
                                   KolibriDifficultyLevel level,
                                   double reward,
                                   int success) {
    (void)level;
    (void)success;
    if (!ai) {
        return -1;
    }
    pthread_mutex_lock(&ai->mutex);
    double blend = 0.2;
    ai->average_reward = clamp01(ai->average_reward * (1.0 - blend) + reward * blend);
    pthread_mutex_unlock(&ai->mutex);
    return 0;
}

int kolibri_ai_add_formula(KolibriAI *ai, const Formula *formula) {
    if (!ai || !formula) {
        return -1;
    }
    pthread_mutex_lock(&ai->mutex);
    int rc = formula_collection_add(ai->library, formula);
    if (rc == 0) {
        recompute_average_reward(ai);
    }
    pthread_mutex_unlock(&ai->mutex);
    return rc;
}

Formula *kolibri_ai_get_best_formula(KolibriAI *ai) {
    if (!ai || !ai->library || ai->library->count == 0) {
        return NULL;
    }
    pthread_mutex_lock(&ai->mutex);
    size_t best_index = 0;
    double best_score = ai->library->formulas[0].effectiveness;
    for (size_t i = 1; i < ai->library->count; ++i) {
        double score = ai->library->formulas[i].effectiveness;
        if (score > best_score) {
            best_score = score;
            best_index = i;
        }
    }
    Formula *result = &ai->library->formulas[best_index];
    pthread_mutex_unlock(&ai->mutex);
    return result;
}

char *kolibri_ai_serialize_state(const KolibriAI *ai) {
    if (!ai) {
        return NULL;
    }
    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    double average = ai->average_reward;
    size_t count = ai->library ? ai->library->count : 0;
    size_t iterations = ai->iterations;
    double exploration = ai->exploration_rate;
    double exploitation = ai->exploitation_rate;
    int running = ai->running;
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    char buffer[256];
    double planning = clamp01(average * 0.9 + exploitation * 0.1);
    double recent_poe = clamp01(average + 0.1);
    double recent_mdl = clamp01(1.0 - average);

    int written = snprintf(buffer,
                           sizeof(buffer),
                           "{\"average_reward\":%.6f,\"formula_count\":%zu,"
                           "\"iterations\":%zu,\"exploration_rate\":%.6f,"
                           "\"exploitation_rate\":%.6f,\"planning_score\":%.6f,"
                           "\"recent_poe\":%.6f,\"recent_mdl\":%.6f,\"running\":%d}",
                           average,
                           count,
                           iterations,
                           exploration,
                           exploitation,
                           planning,
                           recent_poe,
                           recent_mdl,
                           running);
    if (written < 0) {
        return NULL;
    }
    size_t needed = (size_t)written + 1;
    char *json = malloc(needed);
    if (!json) {
        return NULL;
    }
    memcpy(json, buffer, needed);
    return json;
}

char *kolibri_ai_serialize_formulas(const KolibriAI *ai, size_t max_results) {
    if (!ai) {
        return NULL;
    }
    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    size_t count = ai->library ? ai->library->count : 0;
    size_t limit = max_results && max_results < count ? max_results : count;
    Formula const **top = NULL;
    if (limit > 0 && ai->library) {
        top = calloc(limit, sizeof(*top));
        if (top) {
            formula_collection_get_top(ai->library, top, limit);
        }
    }
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    size_t capacity = 160 + limit * 160;
    char *json = malloc(capacity);
    if (!json) {
        free(top);
        return NULL;
    }
    size_t offset = 0;
    offset += (size_t)snprintf(json + offset, capacity - offset, "{\"count\":%zu,\"formulas\":[", limit);
    for (size_t i = 0; top && i < limit; ++i) {
        if (i > 0) {
            offset += (size_t)snprintf(json + offset, capacity - offset, ",");
        }
        const Formula *formula = top[i];
        offset += (size_t)snprintf(json + offset,
                                   capacity - offset,
                                   "{\"id\":\"%s\",\"effectiveness\":%.6f}",
                                   formula ? formula->id : "",
                                   formula ? formula->effectiveness : 0.0);
    }
    offset += (size_t)snprintf(json + offset, capacity - offset, "]}");
    free(top);
    return json;
}
