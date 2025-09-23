#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uuid/uuid.h>
#include <stdint.h>

#include "decimal_cell.h"
#include "formula.h"
#include "formula_advanced.h"

uint64_t now_ms(void) {
    return (uint64_t)time(NULL) * 1000ULL;
}

static Formula* create_text_formula(const char* content) {
    Formula* formula = calloc(1, sizeof(Formula));
    assert(formula);

    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse(uuid, formula->id);
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    if (content) {
        strncpy(formula->content, content, sizeof(formula->content) - 1);
    }
    formula->created_at = time(NULL);
    return formula;
}

static const char* find_resource(const char* name, char* buffer, size_t size) {
    const char* prefixes[] = {"", "../", "../../"};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        snprintf(buffer, size, "%s%s", prefixes[i], name);
        FILE* file = fopen(buffer, "rb");
        if (file) {
            fclose(file);
            return buffer;
        }
    }
    return NULL;
}

static void test_training_pipeline_integration(void) {
    FormulaCollection* collection = formula_collection_create(4);
    assert(collection);

    FormulaTrainingPipeline* pipeline = formula_training_pipeline_create(6);
    assert(pipeline);

    char path_buffer[256];
    const char* dataset_path = find_resource("learning_data.json", path_buffer, sizeof(path_buffer));
    assert(dataset_path);
    assert(formula_training_pipeline_load_dataset(pipeline, dataset_path) == 0);

    const char* weights_path = find_resource("mlp_weights.bin", path_buffer, sizeof(path_buffer));
    if (weights_path) {
        assert(formula_training_pipeline_load_weights(pipeline, weights_path) == 0);
    }

    FormulaMemoryFact fact = {0};
    strncpy(fact.fact_id, "client_case", sizeof(fact.fact_id) - 1);
    strncpy(fact.description, "клиентский запрос", sizeof(fact.description) - 1);
    fact.importance = 0.8;
    fact.reward = 0.5;
    fact.timestamp = time(NULL);

    FormulaMemorySnapshot snapshot = formula_memory_snapshot_clone(&fact, 1);
    assert(snapshot.facts);

    assert(formula_training_pipeline_prepare(pipeline, collection, &snapshot, 3) == 0);
    assert(formula_training_pipeline_evaluate(pipeline, collection) == 0);
    FormulaHypothesis* best = formula_training_pipeline_select_best(pipeline);
    assert(best);
    assert(best->experience.reward >= 0.0);
    assert(formula_training_pipeline_record_experience(pipeline, &best->experience) == 0);

    formula_memory_snapshot_release(&snapshot);
    formula_training_pipeline_destroy(pipeline);
    formula_collection_destroy(collection);
}

static void test_text_formula_roundtrip(void) {
    Formula* formula = create_text_formula("f(x) = 2 * x^2 + 3");
    assert(formula);
    assert(formula->representation == FORMULA_REPRESENTATION_TEXT);

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

    Formula* generated = create_text_formula("f(x) = 5 * x + 1");
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
    test_training_pipeline_integration();
    return 0;
}
