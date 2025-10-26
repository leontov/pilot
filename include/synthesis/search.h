/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_SYNTHESIS_SEARCH_H
#define KOLIBRI_SYNTHESIS_SEARCH_H

#include <stddef.h>
#include <stdint.h>

#include "formula_core.h"

typedef struct FormulaMemorySnapshot FormulaMemorySnapshot;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t max_candidates;
    uint32_t max_terms;
    uint32_t max_coefficient;
    uint32_t max_formula_length;
    double base_effectiveness;
} FormulaSearchConfig;

FormulaSearchConfig formula_search_config_default(void);

typedef int (*formula_search_emit_fn)(const Formula *formula, void *user_data);

typedef struct {
    uint32_t max_mutations;
    uint32_t max_adjustment;
} FormulaMutationConfig;

FormulaMutationConfig formula_mutation_config_default(void);

typedef struct {
    double w1;
    double w2;
    double w3;
    double w4;
} FormulaScoreWeights;

FormulaScoreWeights formula_score_weights_default(void);

double formula_search_compute_score(const FormulaScoreWeights *weights,
                                   double poe,
                                   double mdl,
                                   double runtime,
                                   double gas_used);

typedef struct {
    uint32_t max_depth;
    uint32_t rollouts;
    double exploration;
} FormulaMctsConfig;

typedef struct {
    uint32_t actions[8];
    size_t length;
    double value;
} FormulaSearchPlan;

FormulaMctsConfig formula_mcts_config_default(void);

int formula_search_plan_mcts(const FormulaCollection *library,
                             const FormulaMemorySnapshot *memory,
                             const FormulaMctsConfig *config,
                             FormulaSearchPlan *out_plan);

size_t formula_search_enumerate(const FormulaCollection *library,
                                const FormulaMemorySnapshot *memory,
                                const FormulaSearchConfig *config,
                                formula_search_emit_fn emit,
                                void *user_data);

size_t formula_search_mutate(const FormulaCollection *library,
                             const FormulaMemorySnapshot *memory,
                             const FormulaMutationConfig *config,
                             formula_search_emit_fn emit,
                             void *user_data);

#ifdef __cplusplus
}
#endif

#endif
