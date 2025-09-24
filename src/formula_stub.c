/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

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

void formula_clear(Formula *formula) WEAK_ATTR;
int formula_copy(Formula *dest, const Formula *src) WEAK_ATTR;
FormulaCollection *formula_collection_create(size_t initial_capacity) WEAK_ATTR;
void formula_collection_destroy(FormulaCollection *collection) WEAK_ATTR;
int formula_collection_add(FormulaCollection *collection, const Formula *formula) WEAK_ATTR;
size_t formula_collection_get_top(const FormulaCollection *collection,
                                  const Formula **out_formulas,
                                  size_t max_results) WEAK_ATTR;

static void formula_copy_string(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t len = 0;
    size_t limit = dest_size - 1;
    while (len < limit && src[len] != '\0') {
        len++;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

void formula_clear(Formula *formula) {
    if (!formula) {
        return;
    }

    free(formula->coefficients);
    formula->coefficients = NULL;
    formula->coeff_count = 0;

    free(formula->expression);
    formula->expression = NULL;
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

    if (src->representation == FORMULA_REPRESENTATION_TEXT) {
        formula_copy_string(dest->content, sizeof(dest->content), src->content);
    } else if (src->representation == FORMULA_REPRESENTATION_ANALYTIC) {
        dest->coeff_count = src->coeff_count;
        if (src->coeff_count > 0 && src->coefficients) {
            dest->coefficients = malloc(sizeof(double) * src->coeff_count);
            if (!dest->coefficients) {
                formula_clear(dest);
                return -1;
            }
            memcpy(dest->coefficients, src->coefficients, sizeof(double) * src->coeff_count);
        }

        if (src->expression) {
            size_t len = strlen(src->expression);
            dest->expression = malloc(len + 1);
            if (!dest->expression) {
                formula_clear(dest);
                return -1;
            }
            memcpy(dest->expression, src->expression, len + 1);
        }
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

static void formula_collection_update_top(FormulaCollection *collection, size_t index) {
    if (!collection || index >= collection->count) {
        return;
    }
    collection->best_indices[0] = 0;
    collection->best_indices[1] = (collection->count > 1) ? 1 : 0;
    collection->best_count = collection->count < 2 ? collection->count : 2;
    if (collection->best_count == 0) {
        return;
    }
    size_t best = collection->best_indices[0];
    size_t second = collection->best_count > 1 ? collection->best_indices[1] : best;
    for (size_t i = 0; i < collection->count; ++i) {
        double score = collection->formulas[i].effectiveness;
        if (score > collection->formulas[best].effectiveness) {
            second = best;
            best = i;
        } else if (collection->best_count > 1 && score > collection->formulas[second].effectiveness && i != best) {
            second = i;
        }
    }
    collection->best_indices[0] = best;
    collection->best_indices[1] = second;
}

int formula_collection_add(FormulaCollection *collection, const Formula *formula) {
    if (!collection || !formula) {
        return -1;
    }
    if (collection->count >= collection->capacity) {
        size_t new_capacity = collection->capacity ? collection->capacity * 2 : 4;
        Formula *new_array = realloc(collection->formulas, new_capacity * sizeof(Formula));
        if (!new_array) {
            return -1;
        }
        memset(new_array + collection->capacity, 0, (new_capacity - collection->capacity) * sizeof(Formula));
        collection->formulas = new_array;
        collection->capacity = new_capacity;
    }
    Formula *dest = &collection->formulas[collection->count];
    if (formula_copy(dest, formula) != 0) {
        memset(dest, 0, sizeof(*dest));
        return -1;
    }
    collection->count++;
    formula_collection_update_top(collection, collection->count - 1);
    return 0;
}

size_t formula_collection_get_top(const FormulaCollection *collection,
                                  const Formula **out_formulas,
                                  size_t max_results) {
    if (!collection || !out_formulas || max_results == 0) {
        return 0;
    }
    size_t available = collection->count < max_results ? collection->count : max_results;
    for (size_t i = 0; i < available; ++i) {
        size_t best_index = i;
        double best_score = -1.0;
        for (size_t j = 0; j < collection->count; ++j) {
            int already_selected = 0;
            for (size_t k = 0; k < i; ++k) {
                if (out_formulas[k] == &collection->formulas[j]) {
                    already_selected = 1;
                    break;
                }
            }
            if (already_selected) {
                continue;
            }
            double score = collection->formulas[j].effectiveness;
            if (score > best_score) {
                best_score = score;
                best_index = j;
            }
        }
        out_formulas[i] = &collection->formulas[best_index];
    }
    return available;
}

