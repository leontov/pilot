#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decimal_cell.h"
#include "formula.h"
#include "formula_advanced.h"

static void test_text_formula_roundtrip(void) {
    Formula* formula = generate_random_formula(3);
    assert(formula);
    assert(formula->representation == FORMULA_REPRESENTATION_TEXT);

    strcpy(formula->content, "f(x) = 2 * x^2 + 3");
    formula->effectiveness = 0.5;

    char* json = serialize_formula(formula);
    assert(json);

    Formula* restored = deserialize_formula(json);
    assert(restored);
    assert(restored->representation == FORMULA_REPRESENTATION_TEXT);
    assert(strcmp(restored->content, formula->content) == 0);
    assert(fabs(restored->effectiveness - formula->effectiveness) < 1e-9);

    formula_clear(formula);
    free(formula);
    formula_clear(restored);
    free(restored);
    free(json);
}

static void test_formula_collection_copy(void) {
    FormulaCollection* collection = formula_collection_create(1);
    assert(collection);

    Formula* generated = generate_random_formula(2);
    assert(generated);
    generated->effectiveness = 0.75;
    assert(formula_collection_add(collection, generated) == 0);

    assert(collection->count == 1);
    Formula* stored = &collection->formulas[0];
    assert(stored->representation == FORMULA_REPRESENTATION_TEXT);
    assert(fabs(stored->effectiveness - 0.75) < 1e-9);
    assert(strcmp(stored->content, generated->content) == 0);

    formula_clear(generated);
    free(generated);
    formula_collection_destroy(collection);
}

static void test_analytic_formula_flow(void) {
    Formula* analytic = formula_create(FORMULA_LINEAR, 2);
    assert(analytic);

    analytic->coefficients[0] = 1.5;
    analytic->coefficients[1] = -0.5;
    analytic->expression = strdup("f(x) = 1.5 * x - 0.5");
    assert(analytic->expression);

    double value = formula_evaluate(analytic, 2.0);
    assert(fabs(value - (1.5 * 2.0 - 0.5)) < 1e-9);

    DecimalCell* cell1 = decimal_cell_create(1.0, -10.0, 10.0);
    DecimalCell* cell2 = decimal_cell_create(2.0, -10.0, 10.0);
    assert(cell1 && cell2);
    DecimalCell* cells[] = {cell1, cell2};

    double effectiveness = formula_calculate_effectiveness(analytic, cells, 2);
    assert(effectiveness > 0.0);

    Formula copy = {0};
    assert(formula_copy(&copy, analytic) == 0);
    assert(copy.representation == FORMULA_REPRESENTATION_ANALYTIC);
    assert(copy.coeff_count == analytic->coeff_count);
    assert(copy.coefficients && copy.coefficients != analytic->coefficients);
    for (size_t i = 0; i < copy.coeff_count; i++) {
        assert(fabs(copy.coefficients[i] - analytic->coefficients[i]) < 1e-9);
    }
    assert((analytic->expression && copy.expression && strcmp(copy.expression, analytic->expression) == 0) ||
           (!analytic->expression && copy.expression == NULL));

    char* json = serialize_formula(analytic);
    assert(json);
    Formula* restored = deserialize_formula(json);
    assert(restored);
    assert(restored->representation == FORMULA_REPRESENTATION_ANALYTIC);
    assert(restored->coeff_count == analytic->coeff_count);
    for (size_t i = 0; i < restored->coeff_count; i++) {
        assert(fabs(restored->coefficients[i] - analytic->coefficients[i]) < 1e-9);
    }
    if (analytic->expression) {
        assert(restored->expression);
        assert(strcmp(restored->expression, analytic->expression) == 0);
    }

    formula_clear(&copy);
    formula_clear(restored);
    free(restored);
    free(json);

    formula_destroy(analytic);
    decimal_cell_destroy(cell1);
    decimal_cell_destroy(cell2);
}

int main(void) {
    test_text_formula_roundtrip();
    test_formula_collection_copy();
    test_analytic_formula_flow();
    return 0;
}
