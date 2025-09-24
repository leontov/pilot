// Copyright (c) 2024 Кочуров Владислав Евгеньевич

#include "formula.h"
#include "synthesis/search.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MAX_CANDIDATES 32

typedef struct {
    char contents[MAX_CANDIDATES][256];
    size_t count;
} candidate_buffer_t;

static int collect_formula(const Formula *formula, void *user_data) {
    candidate_buffer_t *buffer = user_data;
    assert(buffer != NULL);
    if (!formula || buffer->count >= MAX_CANDIDATES) {
        return 1;
    }
    for (size_t i = 0; i < buffer->count; ++i) {
        if (strcmp(buffer->contents[i], formula->content) == 0) {
            return 0;
        }
    }
    strncpy(buffer->contents[buffer->count],
            formula->content,
            sizeof(buffer->contents[buffer->count]) - 1);
    buffer->contents[buffer->count][sizeof(buffer->contents[buffer->count]) - 1] = '\0';
    buffer->count++;
    return 0;
}

int main(void) {
    FormulaCollection *library = formula_collection_create(4);
    assert(library != NULL);

    Formula seed = {0};
    seed.representation = FORMULA_REPRESENTATION_TEXT;
    strncpy(seed.id, "seed.1", sizeof(seed.id) - 1);
    strncpy(seed.content, "f(x) = x + 1", sizeof(seed.content) - 1);
    seed.effectiveness = 0.55;
    seed.created_at = 1700000000;
    assert(formula_collection_add(library, &seed) == 0);

    FormulaMemoryFact facts[1] = {0};
    strncpy(facts[0].fact_id, "ctx-1", sizeof(facts[0].fact_id) - 1);
    strncpy(facts[0].description, "increment", sizeof(facts[0].description) - 1);
    facts[0].importance = 0.6;
    facts[0].reward = 0.45;
    facts[0].timestamp = 1700000000;
    FormulaMemorySnapshot snapshot = {.facts = facts, .count = 1};

    candidate_buffer_t buffer = {0};
    FormulaSearchConfig config = formula_search_config_default();
    config.max_candidates = 6;
    size_t enumerated =
        formula_search_enumerate(library, &snapshot, &config, collect_formula, &buffer);
    assert(enumerated > 0);

    FormulaMutationConfig mutation = formula_mutation_config_default();
    mutation.max_mutations = 6;
    size_t mutated =
        formula_search_mutate(library, &snapshot, &mutation, collect_formula, &buffer);
    assert(mutated > 0);

    FormulaScoreWeights weights = formula_score_weights_default();
    double strong = formula_search_compute_score(&weights, 0.9, 0.1, 0.1, 0.0);
    double weak = formula_search_compute_score(&weights, 0.4, 0.4, 0.3, 0.0);
    assert(strong > weak);

    FormulaSearchPlan plan;
    FormulaMctsConfig planner = formula_mcts_config_default();
    int plan_rc = formula_search_plan_mcts(library, &snapshot, &planner, &plan);
    assert(plan_rc == 0);
    assert(plan.length > 0);

    formula_collection_destroy(library);
    return 0;
}
