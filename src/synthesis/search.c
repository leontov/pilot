#include "synthesis/search.h"

#include <stdlib.h>
#include <string.h>

#include "formula.h"

FormulaSearchConfig formula_search_config_default(void) {
    FormulaSearchConfig cfg = {
        .max_candidates = 32,
        .max_terms = 4,
        .max_coefficient = 9,
        .max_formula_length = 64,
        .base_effectiveness = 0.1,
    };
    return cfg;
}

FormulaMutationConfig formula_mutation_config_default(void) {
    FormulaMutationConfig cfg = {
        .max_mutations = 8,
        .max_adjustment = 3,
    };
    return cfg;
}

FormulaScoreWeights formula_score_weights_default(void) {
    FormulaScoreWeights weights = {
        .w1 = 1.0,
        .w2 = 1.0,
        .w3 = 0.0,
        .w4 = 0.0,
    };
    return weights;
}

double formula_search_compute_score(const FormulaScoreWeights *weights,
                                   double poe,
                                   double mdl,
                                   double runtime,
                                   double gas_used) {
    FormulaScoreWeights local = weights ? *weights : formula_score_weights_default();
    double score = 0.0;
    score += local.w1 * poe;
    score -= local.w2 * mdl;
    score -= local.w3 * runtime;
    score -= local.w4 * gas_used;
    return score;
}

FormulaMctsConfig formula_mcts_config_default(void) {
    FormulaMctsConfig cfg = {
        .max_depth = 1,
        .rollouts = 0,
        .exploration = 0.0,
    };
    return cfg;
}

int formula_search_plan_mcts(const FormulaCollection *library,
                             const FormulaMemorySnapshot *memory,
                             const FormulaMctsConfig *config,
                             FormulaSearchPlan *out_plan) {
    (void)library;
    (void)memory;
    if (!out_plan) {
        return -1;
    }
    FormulaMctsConfig local_cfg = config ? *config : formula_mcts_config_default();
    out_plan->length = local_cfg.max_depth > 0 ? 1 : 0;
    for (size_t i = 0; i < out_plan->length && i < sizeof(out_plan->actions) / sizeof(out_plan->actions[0]); ++i) {
        out_plan->actions[i] = 0;
    }
    out_plan->value = 0.0;
    return 0;
}

static size_t emit_library_formulas(const FormulaCollection *library,
                                    formula_search_emit_fn emit,
                                    void *user_data,
                                    size_t limit) {
    if (!library || !emit || limit == 0) {
        return 0;
    }
    size_t emitted = 0;
    for (size_t i = 0; i < library->count && emitted < limit; ++i) {
        if (emit(&library->formulas[i], user_data) != 0) {
            break;
        }
        emitted += 1;
    }
    return emitted;
}

size_t formula_search_enumerate(const FormulaCollection *library,
                                const FormulaMemorySnapshot *memory,
                                const FormulaSearchConfig *config,
                                formula_search_emit_fn emit,
                                void *user_data) {
    (void)memory;
    FormulaSearchConfig local_cfg = config ? *config : formula_search_config_default();
    return emit_library_formulas(library, emit, user_data, local_cfg.max_candidates);
}

size_t formula_search_mutate(const FormulaCollection *library,
                             const FormulaMemorySnapshot *memory,
                             const FormulaMutationConfig *config,
                             formula_search_emit_fn emit,
                             void *user_data) {
    (void)memory;
    FormulaMutationConfig local_cfg = config ? *config : formula_mutation_config_default();
    (void)local_cfg;
    return emit_library_formulas(library, emit, user_data, library ? library->count : 0);
}
