/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_AI_H
#define KOLIBRI_AI_H

#include <stddef.h>
#include <stdint.h>

#include "formula_core.h"
#include "synthesis/selfplay.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KolibriAI KolibriAI;
struct kolibri_config_t;

#ifndef KOLIBRI_AI_SELFPLAY_CONFIG_DEFINED
#define KOLIBRI_AI_SELFPLAY_CONFIG_DEFINED
typedef struct {
    uint32_t tasks_per_iteration;
    uint32_t max_difficulty;
} KolibriAISelfplayConfig;
#endif

typedef struct {
    KolibriSelfplayTask task;
    double predicted_result;
    double error;
    double reward;
    int success;
} KolibriAISelfplayInteraction;

#define KOLIBRI_DIFFICULTY_COUNT 4

typedef enum {
    KOLIBRI_DIFFICULTY_FOUNDATION = 0,
    KOLIBRI_DIFFICULTY_SKILLS = 1,
    KOLIBRI_DIFFICULTY_ADVANCED = 2,
    KOLIBRI_DIFFICULTY_CHALLENGE = 3
} KolibriDifficultyLevel;

KolibriAI *kolibri_ai_create(const struct kolibri_config_t *cfg);
void kolibri_ai_destroy(KolibriAI *ai);

void kolibri_ai_start(KolibriAI *ai);
void kolibri_ai_stop(KolibriAI *ai);
void kolibri_ai_process_iteration(KolibriAI *ai);
void kolibri_ai_set_selfplay_config(KolibriAI *ai, const KolibriAISelfplayConfig *config);
void kolibri_ai_record_interaction(KolibriAI *ai, const KolibriAISelfplayInteraction *interaction);
void kolibri_ai_apply_config(KolibriAI *ai, const struct kolibri_config_t *cfg);

KolibriDifficultyLevel kolibri_ai_plan_actions(KolibriAI *ai, double *expected_reward);
void kolibri_ai_apply_reinforcement(KolibriAI *ai,
                                    KolibriDifficultyLevel level,
                                    double reward,
                                    int success);

int kolibri_ai_add_formula(KolibriAI *ai, const Formula *formula);
Formula *kolibri_ai_get_best_formula(KolibriAI *ai);



char *kolibri_ai_serialize_state(const KolibriAI *ai);
char *kolibri_ai_serialize_formulas(const KolibriAI *ai, size_t max_results);



#ifdef __cplusplus
}
#endif

#endif
