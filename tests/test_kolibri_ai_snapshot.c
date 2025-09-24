/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "kolibri_ai.h"
#include "formula.h"
#include "synthesis/search.h"

#include <assert.h>
#include <json-c/json.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static KolibriAI *create_ai_with_limit(uint32_t limit) {
    kolibri_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.search = formula_search_config_default();
    cfg.ai.snapshot_limit = limit;
    return kolibri_ai_create(&cfg);
}

static void record_interaction(KolibriAI *ai, size_t index) {
    KolibriAISelfplayInteraction interaction;
    memset(&interaction, 0, sizeof(interaction));
    snprintf(interaction.task.description,
             sizeof(interaction.task.description),
             "interaction-%zu",
             index);
    interaction.predicted_result = 0.1 * (double)index;
    interaction.reward = 0.05 * (double)index;
    interaction.task.expected_result = 0.02 * (double)index;
    kolibri_ai_record_interaction(ai, &interaction);
}

static void reinforce_formula(KolibriAI *ai,
                              const char *id,
                              const char *content,
                              double reward,
                              double poe,
                              double mdl) {
    Formula formula;
    memset(&formula, 0, sizeof(formula));
    formula.representation = FORMULA_REPRESENTATION_TEXT;
    snprintf(formula.id, sizeof(formula.id), "%s", id);
    snprintf(formula.content, sizeof(formula.content), "%s", content);
    formula.effectiveness = reward;
    formula.created_at = (time_t)1700000000;
    formula.tests_passed = 1;
    formula.confirmations = 1;

    FormulaExperience experience;
    memset(&experience, 0, sizeof(experience));
    experience.reward = reward;
    experience.poe = poe;
    experience.mdl = mdl;
    snprintf(experience.source, sizeof(experience.source), "reinforcement");
    snprintf(experience.task_id, sizeof(experience.task_id), "%s", id);

    assert(kolibri_ai_apply_reinforcement(ai, &formula, &experience) == 0);
}

static void add_manual_formula(KolibriAI *ai) {
    Formula manual;
    memset(&manual, 0, sizeof(manual));
    manual.representation = FORMULA_REPRESENTATION_TEXT;
    snprintf(manual.id, sizeof(manual.id), "manual.formula");
    snprintf(manual.content, sizeof(manual.content), "g(x)=x*3");
    manual.effectiveness = 0.42;
    manual.created_at = (time_t)1710000000;
    manual.tests_passed = 2;
    manual.confirmations = 1;
    assert(kolibri_ai_add_formula(ai, &manual) == 0);
}

static void assert_double_close(double lhs, double rhs) {
    assert(fabs(lhs - rhs) < 1e-9);
}

static void assert_field_doubles(struct json_object *lhs,
                                 struct json_object *rhs,
                                 const char *key) {
    struct json_object *lhs_field = NULL;
    struct json_object *rhs_field = NULL;
    assert(json_object_object_get_ex(lhs, key, &lhs_field));
    assert(json_object_object_get_ex(rhs, key, &rhs_field));
    assert_double_close(json_object_get_double(lhs_field),
                        json_object_get_double(rhs_field));
}

static void assert_field_int64(struct json_object *lhs,
                               struct json_object *rhs,
                               const char *key) {
    struct json_object *lhs_field = NULL;
    struct json_object *rhs_field = NULL;
    assert(json_object_object_get_ex(lhs, key, &lhs_field));
    assert(json_object_object_get_ex(rhs, key, &rhs_field));
    assert(json_object_get_int64(lhs_field) ==
           json_object_get_int64(rhs_field));
}

static void assert_string_equal(struct json_object *obj,
                                const char *key,
                                const char *expected) {
    struct json_object *field = NULL;
    assert(json_object_object_get_ex(obj, key, &field));
    assert(strcmp(json_object_get_string(field), expected) == 0);
}

static void compare_formula_arrays(struct json_object *lhs,
                                   struct json_object *rhs) {
    size_t lhs_len = json_object_array_length(lhs);
    size_t rhs_len = json_object_array_length(rhs);
    assert(lhs_len == rhs_len);
    for (size_t i = 0; i < lhs_len; ++i) {
        struct json_object *lhs_entry = json_object_array_get_idx(lhs, (int)i);
        struct json_object *rhs_entry = json_object_array_get_idx(rhs, (int)i);
        assert(json_object_is_type(lhs_entry, json_type_object));
        assert(json_object_is_type(rhs_entry, json_type_object));
        struct json_object *lhs_field = NULL;
        struct json_object *rhs_field = NULL;
        assert(json_object_object_get_ex(lhs_entry, "id", &lhs_field));
        assert(json_object_object_get_ex(rhs_entry, "id", &rhs_field));
        assert(strcmp(json_object_get_string(lhs_field),
                      json_object_get_string(rhs_field)) == 0);
        assert(json_object_object_get_ex(lhs_entry,
                                         "effectiveness",
                                         &lhs_field));
        assert(json_object_object_get_ex(rhs_entry,
                                         "effectiveness",
                                         &rhs_field));
        assert_double_close(json_object_get_double(lhs_field),
                            json_object_get_double(rhs_field));
    }
}

static void compare_dataset_arrays(struct json_object *lhs,
                                   struct json_object *rhs) {
    size_t lhs_len = json_object_array_length(lhs);
    size_t rhs_len = json_object_array_length(rhs);
    assert(lhs_len == rhs_len);
    for (size_t i = 0; i < lhs_len; ++i) {
        struct json_object *lhs_entry = json_object_array_get_idx(lhs, (int)i);
        struct json_object *rhs_entry = json_object_array_get_idx(rhs, (int)i);
        assert(json_object_is_type(lhs_entry, json_type_object));
        assert(json_object_is_type(rhs_entry, json_type_object));
        assert_field_doubles(lhs_entry, rhs_entry, "reward");
        assert_field_doubles(lhs_entry, rhs_entry, "poe");
        assert_field_doubles(lhs_entry, rhs_entry, "mdl");
        assert_field_int64(lhs_entry, rhs_entry, "timestamp");

        struct json_object *lhs_field = NULL;
        struct json_object *rhs_field = NULL;
        assert(json_object_object_get_ex(lhs_entry, "prompt", &lhs_field));
        assert(json_object_object_get_ex(rhs_entry, "prompt", &rhs_field));
        assert(strcmp(json_object_get_string(lhs_field),
                      json_object_get_string(rhs_field)) == 0);
        assert(json_object_object_get_ex(lhs_entry, "response", &lhs_field));
        assert(json_object_object_get_ex(rhs_entry, "response", &rhs_field));
        assert(strcmp(json_object_get_string(lhs_field),
                      json_object_get_string(rhs_field)) == 0);
    }
}

static void compare_memory_arrays(struct json_object *lhs,
                                  struct json_object *rhs) {
    size_t lhs_len = json_object_array_length(lhs);
    size_t rhs_len = json_object_array_length(rhs);
    assert(lhs_len == rhs_len);
    for (size_t i = 0; i < lhs_len; ++i) {
        struct json_object *lhs_entry = json_object_array_get_idx(lhs, (int)i);
        struct json_object *rhs_entry = json_object_array_get_idx(rhs, (int)i);
        assert(json_object_is_type(lhs_entry, json_type_object));
        assert(json_object_is_type(rhs_entry, json_type_object));
        assert_field_doubles(lhs_entry, rhs_entry, "salience");
        assert_field_int64(lhs_entry, rhs_entry, "last_updated");

        struct json_object *lhs_field = NULL;
        struct json_object *rhs_field = NULL;
        assert(json_object_object_get_ex(lhs_entry, "key", &lhs_field));
        assert(json_object_object_get_ex(rhs_entry, "key", &rhs_field));
        assert(strcmp(json_object_get_string(lhs_field),
                      json_object_get_string(rhs_field)) == 0);
        assert(json_object_object_get_ex(lhs_entry, "value", &lhs_field));
        assert(json_object_object_get_ex(rhs_entry, "value", &rhs_field));
        assert(strcmp(json_object_get_string(lhs_field),
                      json_object_get_string(rhs_field)) == 0);
    }
}

int main(void) {
    KolibriAI *ai = create_ai_with_limit(3);
    assert(ai != NULL);

    add_manual_formula(ai);

    for (size_t i = 1; i <= 3; ++i) {
        record_interaction(ai, i);
    }

    reinforce_formula(ai, "reinforce.A", "f_A(x)=x+1", 0.85, 0.8, 0.1);
    reinforce_formula(ai, "reinforce.B", "f_B(x)=x+2", 0.9, 0.75, 0.2);

    char *snapshot = kolibri_ai_export_snapshot(ai);
    assert(snapshot != NULL);

    struct json_object *original = json_tokener_parse(snapshot);
    assert(original != NULL);

    struct json_object *dataset_obj = NULL;
    struct json_object *entries = NULL;
    assert(json_object_object_get_ex(original, "dataset", &dataset_obj));
    assert(json_object_object_get_ex(dataset_obj, "entries", &entries));
    assert(json_object_is_type(entries, json_type_array));
    assert(json_object_array_length(entries) == 3);
    struct json_object *entry = json_object_array_get_idx(entries, 0);
    assert_string_equal(entry, "prompt", "interaction-3");
    entry = json_object_array_get_idx(entries, 1);
    assert_string_equal(entry, "prompt", "f_A(x)=x+1");
    entry = json_object_array_get_idx(entries, 2);
    assert_string_equal(entry, "prompt", "f_B(x)=x+2");

    struct json_object *memory_obj = NULL;
    struct json_object *facts = NULL;
    assert(json_object_object_get_ex(original, "memory", &memory_obj));
    assert(json_object_object_get_ex(memory_obj, "facts", &facts));
    assert(json_object_is_type(facts, json_type_array));
    assert(json_object_array_length(facts) == 2);
    struct json_object *fact = json_object_array_get_idx(facts, 0);
    assert_string_equal(fact, "key", "reinforce.A");
    fact = json_object_array_get_idx(facts, 1);
    assert_string_equal(fact, "key", "reinforce.B");

    KolibriAI *clone = create_ai_with_limit(3);
    assert(clone != NULL);
    assert(kolibri_ai_import_snapshot(clone, snapshot) == 0);

    char *roundtrip_json = kolibri_ai_export_snapshot(clone);
    assert(roundtrip_json != NULL);
    struct json_object *roundtrip = json_tokener_parse(roundtrip_json);
    assert(roundtrip != NULL);

    assert_field_int64(original, roundtrip, "iterations");
    assert_field_doubles(original, roundtrip, "average_reward");
    assert_field_doubles(original, roundtrip, "planning_score");
    assert_field_doubles(original, roundtrip, "recent_poe");
    assert_field_doubles(original, roundtrip, "recent_mdl");

    struct json_object *orig_formulas = NULL;
    struct json_object *clone_formulas = NULL;
    assert(json_object_object_get_ex(original, "formulas", &orig_formulas));
    assert(json_object_object_get_ex(roundtrip, "formulas", &clone_formulas));
    compare_formula_arrays(orig_formulas, clone_formulas);

    struct json_object *clone_dataset = NULL;
    struct json_object *clone_entries = NULL;
    assert(json_object_object_get_ex(roundtrip, "dataset", &clone_dataset));
    assert(json_object_object_get_ex(clone_dataset, "entries", &clone_entries));
    compare_dataset_arrays(entries, clone_entries);

    struct json_object *clone_memory = NULL;
    struct json_object *clone_facts = NULL;
    assert(json_object_object_get_ex(roundtrip, "memory", &clone_memory));
    assert(json_object_object_get_ex(clone_memory, "facts", &clone_facts));
    compare_memory_arrays(facts, clone_facts);

    json_object_put(roundtrip);
    json_object_put(original);
    free(roundtrip_json);
    free(snapshot);

    kolibri_ai_destroy(clone);
    kolibri_ai_destroy(ai);
    return 0;
}
