/* Copyright (c) 2025 Кочуров Владислав Евгеньевич */

#include "formula.h"

void formula_collection_reset_top(FormulaCollection* collection) {
    if (!collection) {
        return;
    }

    collection->best_indices[0] = SIZE_MAX;
    collection->best_indices[1] = SIZE_MAX;
    collection->best_count = 0;
}

void formula_collection_consider_index(FormulaCollection* collection, size_t index) {
    if (!collection || index >= collection->count) {
        return;
    }

    const Formula* candidate = &collection->formulas[index];

    if (collection->best_count == 0) {
        collection->best_indices[0] = index;
        collection->best_count = 1;
        return;
    }

    size_t current_best = collection->best_indices[0];
    const Formula* best_formula = &collection->formulas[current_best];

    if (candidate->effectiveness > best_formula->effectiveness) {
        size_t previous_best = collection->best_indices[0];
        collection->best_indices[0] = index;
        if (collection->best_count == 1) {
            collection->best_indices[1] = previous_best;
            collection->best_count = 2;
        } else {
            collection->best_indices[1] = previous_best;
        }
        return;
    }

    if (collection->best_count == 1) {
        collection->best_indices[1] = index;
        collection->best_count = 2;
        return;
    }

    size_t current_second = collection->best_indices[1];
    const Formula* second_formula = &collection->formulas[current_second];

    if (candidate->effectiveness > second_formula->effectiveness) {
        collection->best_indices[1] = index;
    }
}

void formula_collection_recompute_top(FormulaCollection* collection) {
    if (!collection) {
        return;
    }

    formula_collection_reset_top(collection);
    for (size_t i = 0; i < collection->count; i++) {
        formula_collection_consider_index(collection, i);
    }
}
