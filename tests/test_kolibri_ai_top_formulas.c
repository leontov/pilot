#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "formula.h"

static void prepare_formula(Formula* formula, const char* body, double effectiveness) {
    memset(formula, 0, sizeof(*formula));
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    snprintf(formula->content, sizeof(formula->content), "f(x) = %s", body);
    formula->effectiveness = effectiveness;
}

int main(void) {
    FormulaCollection* collection = formula_collection_create(4);
    assert(collection);

    Formula temp;
    prepare_formula(&temp, "low_a(x)", 0.2);
    assert(formula_collection_add(collection, &temp) == 0);

    prepare_formula(&temp, "low_b(x)", 0.3);
    assert(formula_collection_add(collection, &temp) == 0);

    prepare_formula(&temp, "high_alpha(x)", 0.85);
    assert(formula_collection_add(collection, &temp) == 0);

    prepare_formula(&temp, "high_beta(x)", 0.92);
    assert(formula_collection_add(collection, &temp) == 0);

    assert(collection->count == 4);

    const Formula* top_two[2] = {0};
    size_t received = formula_collection_get_top(collection, top_two, 2);
    assert(received == 2);
    assert(fabs(top_two[0]->effectiveness - 0.92) < 1e-9);
    assert(fabs(top_two[1]->effectiveness - 0.85) < 1e-9);
    assert(strstr(top_two[0]->content, "high_beta"));
    assert(strstr(top_two[1]->content, "high_alpha"));

    Formula combined;
    memset(&combined, 0, sizeof(combined));
    combined.representation = FORMULA_REPRESENTATION_TEXT;
    snprintf(combined.content, sizeof(combined.content),
             "f(x) = (%s) + (%s)",
             top_two[0]->content + 7,
             top_two[1]->content + 7);
    combined.effectiveness = 0.97;

    assert(formula_collection_add(collection, &combined) == 0);
    assert(collection->count == 5);

    const Formula* refreshed_top[2] = {0};
    received = formula_collection_get_top(collection, refreshed_top, 2);
    assert(received == 2);
    assert(fabs(refreshed_top[0]->effectiveness - 0.97) < 1e-9);
    assert(strstr(refreshed_top[0]->content, "high_beta"));
    assert(strstr(refreshed_top[0]->content, "high_alpha"));
    assert(fabs(refreshed_top[1]->effectiveness - 0.92) < 1e-9);

    formula_collection_destroy(collection);
    return 0;
}
