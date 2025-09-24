/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_AI_H
#define KOLIBRI_AI_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "formula.h"
#include "formula_core.h"
#include "synthesis/search.h"
#include "synthesis/selfplay.h"
#include "util/config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KolibriAI KolibriAI;

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

typedef struct {
    double distribution[KOLIBRI_DIFFICULTY_COUNT];
    double success_ema[KOLIBRI_DIFFICULTY_COUNT];
    double reward_ema[KOLIBRI_DIFFICULTY_COUNT];
    uint64_t sample_count[KOLIBRI_DIFFICULTY_COUNT];
    double global_success_ema;
    double integral_error;
    double last_error;
    double temperature;
    double ema_alpha;
    KolibriDifficultyLevel current_level;
} KolibriCurriculumState;

typedef struct {
    char prompt[256];
    char response[512];
    double reward;
    double poe;
    double mdl;
    time_t timestamp;
} KolibriAIDatasetEntry;

typedef struct {
    KolibriAIDatasetEntry *entries;
    size_t count;
    size_t capacity;
} KolibriAIDataset;

typedef struct {
    char key[64];
    char value[256];
    double salience;
    time_t last_updated;
} KolibriMemoryFact;

typedef struct {
    KolibriMemoryFact *facts;
    size_t count;
    size_t capacity;
} KolibriMemoryModule;

KolibriAI *kolibri_ai_create(const kolibri_config_t *cfg);

void kolibri_ai_destroy(KolibriAI *ai);

void kolibri_ai_start(KolibriAI *ai);
void kolibri_ai_stop(KolibriAI *ai);
void kolibri_ai_process_iteration(KolibriAI *ai);
void kolibri_ai_set_selfplay_config(KolibriAI *ai, const KolibriAISelfplayConfig *config);
void kolibri_ai_record_interaction(KolibriAI *ai, const KolibriAISelfplayInteraction *interaction);
void kolibri_ai_apply_config(KolibriAI *ai, const kolibri_config_t *cfg);

KolibriDifficultyLevel kolibri_ai_plan_actions(KolibriAI *ai, double *expected_reward);
int kolibri_ai_apply_reinforcement(KolibriAI *ai,
                                   const Formula *formula,
                                   const FormulaExperience *experience);

int kolibri_ai_add_formula(KolibriAI *ai, const Formula *formula);
Formula *kolibri_ai_get_best_formula(KolibriAI *ai);

char *kolibri_ai_serialize_state(const KolibriAI *ai);
char *kolibri_ai_serialize_formulas(const KolibriAI *ai, size_t max_results);

/* Export the current AI state to a JSON document including formulas, dataset
 * entries, and memory facts. Caller must free the returned string. */
char *kolibri_ai_export_snapshot(const KolibriAI *ai);
/* Import a snapshot previously produced by kolibri_ai_export_snapshot,
 * restoring formulas, dataset entries, and memory facts. */
int kolibri_ai_import_snapshot(KolibriAI *ai, const char *json);
int kolibri_ai_sync_with_neighbor(KolibriAI *ai, const char *base_url);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_AI_H */
