#include "formula.h"

#include <stdlib.h>
#include <string.h>

#ifndef WEAK_ATTR
# if defined(__GNUC__)
#  define WEAK_ATTR __attribute__((weak))
# else
#  define WEAK_ATTR
# endif
#endif

static void formula_reset_metadata(Formula *formula) {
    if (!formula) {
        return;
    }
    formula->effectiveness = 0.0;
    formula->created_at = 0;
    formula->tests_passed = 0;
    formula->confirmations = 0;
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    formula->type = FORMULA_LINEAR;
    formula->content[0] = '\0';
}

void formula_clear(Formula *formula) WEAK_ATTR;
int formula_copy(Formula *dest, const Formula *src) WEAK_ATTR;
FormulaCollection *formula_collection_create(size_t initial_capacity) WEAK_ATTR;
void formula_collection_destroy(FormulaCollection *collection) WEAK_ATTR;
int formula_collection_add(FormulaCollection *collection, const Formula *formula) WEAK_ATTR;
size_t formula_collection_get_top(const FormulaCollection *collection,
                                  const Formula **out_formulas,
                                  size_t max_results) WEAK_ATTR;

void formula_clear(Formula *formula) {
    if (!formula) {
        return;
    }
    free(formula->coefficients);
    free(formula->expression);
    formula->coefficients = NULL;
    formula->expression = NULL;
    formula->coeff_count = 0;
    formula_reset_metadata(formula);
}

static int copy_dynamic_fields(Formula *dest, const Formula *src) {
    if (src->coeff_count > 0 && src->coefficients) {
        dest->coefficients = malloc(sizeof(double) * src->coeff_count);
        if (!dest->coefficients) {
            return -1;
        }
        memcpy(dest->coefficients, src->coefficients, sizeof(double) * src->coeff_count);
    }
    dest->coeff_count = src->coeff_count;

    if (src->expression) {
        size_t len = strlen(src->expression);
        dest->expression = malloc(len + 1);
        if (!dest->expression) {
            free(dest->coefficients);
            dest->coefficients = NULL;
            dest->coeff_count = 0;
            return -1;
        }
        memcpy(dest->expression, src->expression, len + 1);
    }
    return 0;
}

int formula_copy(Formula *dest, const Formula *src) {
    if (!dest || !src) {
        return -1;
    }
    formula_clear(dest);
    memset(dest, 0, sizeof(*dest));

    memcpy(dest->id, src->id, sizeof(dest->id));
    dest->effectiveness = src->effectiveness;
    dest->created_at = src->created_at;
    dest->tests_passed = src->tests_passed;
    dest->confirmations = src->confirmations;
    dest->representation = src->representation;
    dest->type = src->type;
    memcpy(dest->content, src->content, sizeof(dest->content));

    if (copy_dynamic_fields(dest, src) != 0) {
        formula_clear(dest);
        return -1;
    }
    return 0;
}

FormulaCollection *formula_collection_create(size_t initial_capacity) {
    if (initial_capacity == 0) {
        initial_capacity = 4;
    }
    FormulaCollection *collection = calloc(1, sizeof(FormulaCollection));
    if (!collection) {
        return NULL;
    }
    collection->formulas = calloc(initial_capacity, sizeof(Formula));
    if (!collection->formulas) {
        free(collection);
        return NULL;
    }
    collection->capacity = initial_capacity;
    collection->count = 0;
    collection->best_indices[0] = 0;
    collection->best_indices[1] = 0;
    collection->best_count = 0;
    return collection;
}

void formula_collection_destroy(FormulaCollection *collection) {
    if (!collection) {
        return;
    }
    if (collection->formulas) {
        for (size_t i = 0; i < collection->count; ++i) {
            formula_clear(&collection->formulas[i]);
        }
        free(collection->formulas);
    }
    free(collection);
}

static void formula_collection_update_top(FormulaCollection *collection) {
    if (!collection || collection->count == 0) {
        collection->best_count = 0;
        return;
    }
    size_t best = 0;
    size_t second = (collection->count > 1) ? 1 : 0;
    for (size_t i = 1; i < collection->count; ++i) {
        double score = collection->formulas[i].effectiveness;
        if (score > collection->formulas[best].effectiveness) {
            second = best;
            best = i;
        } else if (collection->count > 1 && i != best &&
                   score > collection->formulas[second].effectiveness) {
            second = i;
        }
    }
    collection->best_indices[0] = best;
    collection->best_indices[1] = (collection->count > 1) ? second : best;
    collection->best_count = collection->count < 2 ? collection->count : 2;
}

int formula_collection_add(FormulaCollection *collection, const Formula *formula) {
    if (!collection || !formula) {
        return -1;
    }
    if (collection->count >= collection->capacity) {
        size_t new_capacity = collection->capacity ? collection->capacity * 2 : 4;
        Formula *new_storage = realloc(collection->formulas, new_capacity * sizeof(Formula));
        if (!new_storage) {
            return -1;
        }
        memset(new_storage + collection->capacity, 0, (new_capacity - collection->capacity) * sizeof(Formula));
        collection->formulas = new_storage;
        collection->capacity = new_capacity;
    }
    Formula *slot = &collection->formulas[collection->count];
    memset(slot, 0, sizeof(*slot));
    if (formula_copy(slot, formula) != 0) {
        memset(slot, 0, sizeof(*slot));
        return -1;
    }
    collection->count += 1;
    formula_collection_update_top(collection);
    return 0;
}

size_t formula_collection_get_top(const FormulaCollection *collection,
                                  const Formula **out_formulas,
                                  size_t max_results) {
    if (!collection || !out_formulas || max_results == 0) {
        return 0;
    }
    size_t available = collection->best_count;
    if (available > max_results) {
        available = max_results;
    }
    for (size_t i = 0; i < available; ++i) {
        size_t index = collection->best_indices[i];
        if (index < collection->count) {
            out_formulas[i] = &collection->formulas[index];
        } else {
            out_formulas[i] = NULL;
        }
    }
    return available;
}
