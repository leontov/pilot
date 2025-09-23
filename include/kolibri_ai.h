#ifndef KOLIBRI_AI_H
#define KOLIBRI_AI_H

#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef struct KolibriAI KolibriAI;
struct kolibri_config_t;

typedef struct {
    uint32_t tasks_per_iteration;
    uint32_t max_difficulty;
} KolibriAISelfplayConfig;

typedef struct {
    KolibriSelfplayTask task;
    double predicted_result;
    double error;
    double reward;
    int success;
} KolibriAISelfplayInteraction;

KolibriAI *kolibri_ai_create(void);
void kolibri_ai_destroy(KolibriAI *ai);

void kolibri_ai_start(KolibriAI *ai);
void kolibri_ai_stop(KolibriAI *ai);
void kolibri_ai_process_iteration(KolibriAI *ai);
void kolibri_ai_set_selfplay_config(KolibriAI *ai, const KolibriAISelfplayConfig *config);
void kolibri_ai_record_interaction(KolibriAI *ai, const KolibriAISelfplayInteraction *interaction);
void kolibri_ai_apply_config(KolibriAI *ai, const struct kolibri_config_t *cfg);

int kolibri_ai_add_formula(KolibriAI *ai, const Formula *formula);
Formula *kolibri_ai_get_best_formula(KolibriAI *ai);

char *kolibri_ai_serialize_state(const KolibriAI *ai);
char *kolibri_ai_serialize_formulas(const KolibriAI *ai, size_t max_results);

int kolibri_ai_process_remote_formula(KolibriAI *ai,
                                      const Formula *formula,
                                      const FormulaExperience *experience);
char *kolibri_ai_export_snapshot(const KolibriAI *ai);
int kolibri_ai_import_snapshot(KolibriAI *ai, const char *json_payload);
int kolibri_ai_sync_with_neighbor(KolibriAI *ai, const char *neighbor_base_url);

#ifdef __cplusplus
}
#endif

#endif
