#ifndef KOLIBRI_AI_H
#define KOLIBRI_AI_H

#include <stddef.h>
#include <stdint.h>

#include "formula_core.h"

#define KOLIBRI_AI_LEARNING_DATA_ENV "KOLIBRI_LEARNING_DATA"
#define KOLIBRI_AI_LEARNING_DATA_DEFAULT "learning_data.jsonl"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KolibriAI KolibriAI;

KolibriAI *kolibri_ai_create(void);
void kolibri_ai_destroy(KolibriAI *ai);

void kolibri_ai_start(KolibriAI *ai);
void kolibri_ai_stop(KolibriAI *ai);
void kolibri_ai_process_iteration(KolibriAI *ai);

int kolibri_ai_add_formula(KolibriAI *ai, const Formula *formula);
Formula *kolibri_ai_get_best_formula(KolibriAI *ai);

char *kolibri_ai_serialize_state(const KolibriAI *ai);
char *kolibri_ai_serialize_formulas(const KolibriAI *ai, size_t max_results);

void kolibri_ai_record_interaction(const char *task,
                                   const char *response,
                                   double effectiveness,
                                   int rating);

#ifdef __cplusplus
}
#endif

#endif
