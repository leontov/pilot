#ifndef KOLIBRI_AI_H
#define KOLIBRI_AI_H

#include <stddef.h>
#include <stdint.h>

#include "formula_core.h"
#include "synthesis/search.h"
#include "util/config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KolibriAI KolibriAI;

KolibriAI *kolibri_ai_create(const kolibri_config_t *cfg);
void kolibri_ai_destroy(KolibriAI *ai);

void kolibri_ai_start(KolibriAI *ai);
void kolibri_ai_stop(KolibriAI *ai);
void kolibri_ai_process_iteration(KolibriAI *ai);

int kolibri_ai_add_formula(KolibriAI *ai, const Formula *formula);
Formula *kolibri_ai_get_best_formula(KolibriAI *ai);

void kolibri_ai_set_search_config(KolibriAI *ai, const FormulaSearchConfig *config);

char *kolibri_ai_serialize_state(const KolibriAI *ai);
char *kolibri_ai_serialize_formulas(const KolibriAI *ai, size_t max_results);

#ifdef __cplusplus
}
#endif

#endif
