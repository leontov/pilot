#include "formula.h"

#include <stdlib.h>
#include <string.h>

static char *formula_strdup(const char *src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

const int FORMULA_TYPE_SIMPLE = 0;
const int FORMULA_TYPE_POLYNOMIAL = 1;
const int FORMULA_TYPE_COMPOSITE = 2;
const int FORMULA_TYPE_PERIODIC = 3;

static void formula_collection_reset_top(FormulaCollection *collection) {
    if (!collection) {
        return;
    }

    collection->best_indices[0] = SIZE_MAX;
    collection->best_indices[1] = SIZE_MAX;
    collection->best_count = 0;
}

static void formula_collection_consider_index(FormulaCollection *collection, size_t index) {
    if (!collection || index >= collection->count) {
        return;
    }

    const Formula *candidate = &collection->formulas[index];

    if (collection->best_count == 0) {
        collection->best_indices[0] = index;
        collection->best_count = 1;
        return;
    }

    size_t current_best = collection->best_indices[0];
    const Formula *best_formula = &collection->formulas[current_best];

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
    const Formula *second_formula = &collection->formulas[current_second];

    if (candidate->effectiveness > second_formula->effectiveness) {
        collection->best_indices[1] = index;
    }
}

static void formula_collection_recompute_top(FormulaCollection *collection) {
    if (!collection) {
        return;
    }

    formula_collection_reset_top(collection);
    for (size_t i = 0; i < collection->count; ++i) {
        formula_collection_consider_index(collection, i);
    }
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
        size_t len = strlen(src->content);
        if (len >= sizeof(dest->content)) {
            len = sizeof(dest->content) - 1;
        }
        memcpy(dest->content, src->content, len);
        dest->content[len] = '\0';
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
            dest->expression = formula_strdup(src->expression);
            if (!dest->expression) {
                formula_clear(dest);
                return -1;
            }
        }
    }

    return 0;
}

FormulaCollection *formula_collection_create(size_t initial_capacity) {
    FormulaCollection *collection = malloc(sizeof(FormulaCollection));
    if (!collection) {
        return NULL;
    }

    if (initial_capacity == 0) {
        initial_capacity = 1;
    }

    collection->formulas = calloc(initial_capacity, sizeof(Formula));
    if (!collection->formulas) {
        free(collection);
        return NULL;
    }

    collection->count = 0;
    collection->capacity = initial_capacity;
    formula_collection_reset_top(collection);
    return collection;
}

void formula_collection_destroy(FormulaCollection *collection) {
    if (!collection) {
        return;
    }

    for (size_t i = 0; i < collection->count; ++i) {
        formula_clear(&collection->formulas[i]);
    }
    free(collection->formulas);
    free(collection);
}

int formula_collection_add(FormulaCollection *collection, const Formula *formula) {
    if (!collection || !formula) {
        return -1;
    }

    if (collection->count >= collection->capacity) {
        size_t new_capacity = collection->capacity * 2;
        Formula *resized = realloc(collection->formulas, sizeof(Formula) * new_capacity);
        if (!resized) {
            return -1;
        }
        memset(resized + collection->capacity, 0, sizeof(Formula) * (new_capacity - collection->capacity));
        collection->formulas = resized;
        collection->capacity = new_capacity;
    }

    Formula *dest = &collection->formulas[collection->count];
    if (formula_copy(dest, formula) != 0) {
        memset(dest, 0, sizeof(*dest));
        return -1;
    }

    collection->count++;
    formula_collection_consider_index(collection, collection->count - 1);
    return 0;
}

Formula *formula_collection_find(FormulaCollection *collection, const char *id) {
    if (!collection || !id) {
        return NULL;
    }

    for (size_t i = 0; i < collection->count; ++i) {
        if (strcmp(collection->formulas[i].id, id) == 0) {
            return &collection->formulas[i];
        }
    }
    return NULL;
}

void formula_collection_remove(FormulaCollection *collection, const char *id) {
    if (!collection || !id) {
        return;
    }

    for (size_t i = 0; i < collection->count; ++i) {
        if (strcmp(collection->formulas[i].id, id) == 0) {
            formula_clear(&collection->formulas[i]);
            if (i + 1 < collection->count) {
                memmove(&collection->formulas[i], &collection->formulas[i + 1],
                        sizeof(Formula) * (collection->count - i - 1));
            }
            collection->count--;
            memset(&collection->formulas[collection->count], 0, sizeof(Formula));
            formula_collection_recompute_top(collection);
            break;
        }
    }
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

    size_t produced = 0;
    for (size_t i = 0; i < available; ++i) {
        size_t index = collection->best_indices[i];
        if (index >= collection->count) {
            break;
        }
        out_formulas[produced++] = &collection->formulas[index];
    }
    return produced;
}

int get_formula_type(const char *content) {
    if (!content) {
        return FORMULA_TYPE_SIMPLE;
    }
    if (strstr(content, "sin") || strstr(content, "cos")) {
        return FORMULA_TYPE_PERIODIC;
    }
    if (strstr(content, "^")) {
        return FORMULA_TYPE_POLYNOMIAL;
    }
    if (strstr(content, "+") || strstr(content, "*")) {
        return FORMULA_TYPE_COMPOSITE;
    }
    return FORMULA_TYPE_SIMPLE;
}
