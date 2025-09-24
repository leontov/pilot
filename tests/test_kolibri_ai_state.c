/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "kolibri_ai.h"
#include "formula.h"

#include <assert.h>
#include <json-c/json.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EPSILON 1e-6

static void ensure_contains(const char *json, const char *needle) {
    if (!json || !needle) {
        assert(0 && "invalid input");
    }
    if (!strstr(json, needle)) {
        fprintf(stderr, "expected '%s' in '%s'\n", needle, json);
        assert(0 && "missing field");
    }
}

static int doubles_close(double a, double b) {
    return fabs(a - b) < EPSILON;
}

static int dataset_entry_matches(struct json_object *entry,
                                 const char *prompt,
                                 const char *response,
                                 double reward,
                                 double poe,
                                 double mdl) {
    struct json_object *field = NULL;
    const char *value = NULL;

    if (!json_object_object_get_ex(entry, "prompt", &field)) {
        return 0;
    }
    value = json_object_get_string(field);
    if (!value || strcmp(value, prompt) != 0) {
        return 0;
    }

    if (!json_object_object_get_ex(entry, "response", &field)) {
        return 0;
    }
    value = json_object_get_string(field);
    if (!value || strcmp(value, response) != 0) {
        return 0;
    }

    if (!json_object_object_get_ex(entry, "reward", &field) ||
        !doubles_close(json_object_get_double(field), reward)) {
        return 0;
    }
    if (!json_object_object_get_ex(entry, "poe", &field) ||
        !doubles_close(json_object_get_double(field), poe)) {
        return 0;
    }
    if (!json_object_object_get_ex(entry, "mdl", &field) ||
        !doubles_close(json_object_get_double(field), mdl)) {
        return 0;
    }
    return 1;
}

static int memory_fact_matches(struct json_object *fact,
                               const char *key,
                               const char *value,
                               double salience) {
    struct json_object *field = NULL;
    const char *text = NULL;

    if (!json_object_object_get_ex(fact, "key", &field)) {
        return 0;
    }
    text = json_object_get_string(field);
    if (!text || strcmp(text, key) != 0) {
        return 0;
    }

    if (!json_object_object_get_ex(fact, "value", &field)) {
        return 0;
    }
    text = json_object_get_string(field);
    if (!text || strcmp(text, value) != 0) {
        return 0;
    }

    if (!json_object_object_get_ex(fact, "salience", &field)) {
        return 0;
    }
    return doubles_close(json_object_get_double(field), salience);
}

int main(void) {
    kolibri_config_t cfg = {0};
    cfg.search = formula_search_config_default();
    cfg.search.max_candidates = 2;
    cfg.ai.snapshot_limit = 4;
    KolibriAI *ai = kolibri_ai_create(&cfg);
    assert(ai != NULL);

    kolibri_ai_start(ai);
    struct timespec ts = {0, 100000 * 1000};
    nanosleep(&ts, NULL);
    kolibri_ai_stop(ai);

    Formula formula = {0};
    formula.representation = FORMULA_REPRESENTATION_TEXT;
    strncpy(formula.id, "test.reinforce", sizeof(formula.id) - 1);
    strncpy(formula.content, "1+1", sizeof(formula.content) - 1);

    FormulaExperience experience = {0};
    experience.reward = 0.8;
    experience.poe = 0.9;
    experience.mdl = 0.05;
    assert(kolibri_ai_apply_reinforcement(ai, &formula, &experience) == 0);

    KolibriAISelfplayInteraction interaction = {0};
    strncpy(interaction.task.description,
            "practice addition",
            sizeof(interaction.task.description) - 1);
    interaction.predicted_result = 0.42;
    interaction.reward = 0.4;
    interaction.task.expected_result = 0.37;
    interaction.success = 1;
    kolibri_ai_record_interaction(ai, &interaction);

    char *state = kolibri_ai_serialize_state(ai);
    assert(state != NULL);
    ensure_contains(state, "\"iterations\"");
    ensure_contains(state, "\"formula_count\"");
    ensure_contains(state, "\"planning_score\"");
    ensure_contains(state, "\"recent_poe\"");
    ensure_contains(state, "\"recent_mdl\"");
    free(state);

    char *formulas_json = kolibri_ai_serialize_formulas(ai, 3);
    assert(formulas_json != NULL);
    ensure_contains(formulas_json, "formulas");
    ensure_contains(formulas_json, "kolibri");
    free(formulas_json);

    char *snapshot = kolibri_ai_export_snapshot(ai);
    assert(snapshot != NULL);

    KolibriAI *clone = kolibri_ai_create(&cfg);
    assert(clone != NULL);
    assert(kolibri_ai_import_snapshot(clone, snapshot) == 0);
    free(snapshot);

    char *roundtrip = kolibri_ai_export_snapshot(clone);
    assert(roundtrip != NULL);

    struct json_object *root = json_tokener_parse(roundtrip);
    assert(root != NULL);
    assert(json_object_is_type(root, json_type_object));

    struct json_object *formulas = NULL;
    assert(json_object_object_get_ex(root, "formulas", &formulas));
    assert(json_object_is_type(formulas, json_type_array));
    int formula_found = 0;
    size_t formula_len = json_object_array_length(formulas);
    for (size_t i = 0; i < formula_len; ++i) {
        struct json_object *item = json_object_array_get_idx(formulas, (int)i);
        if (!item || !json_object_is_type(item, json_type_object)) {
            continue;
        }
        struct json_object *id_obj = NULL;
        struct json_object *eff_obj = NULL;
        if (!json_object_object_get_ex(item, "id", &id_obj) ||
            !json_object_object_get_ex(item, "effectiveness", &eff_obj)) {
            continue;
        }
        const char *id = json_object_get_string(id_obj);
        if (id && strcmp(id, "test.reinforce") == 0 &&
            doubles_close(json_object_get_double(eff_obj), 0.8)) {
            formula_found = 1;
            break;
        }
    }
    assert(formula_found);

    struct json_object *dataset = NULL;
    assert(json_object_object_get_ex(root, "dataset", &dataset));
    assert(json_object_is_type(dataset, json_type_object));
    struct json_object *entries = NULL;
    assert(json_object_object_get_ex(dataset, "entries", &entries));
    assert(json_object_is_type(entries, json_type_array));
    int reinforce_entry = 0;
    int interaction_entry = 0;
    size_t entry_len = json_object_array_length(entries);
    for (size_t i = 0; i < entry_len; ++i) {
        struct json_object *entry = json_object_array_get_idx(entries, (int)i);
        if (!entry || !json_object_is_type(entry, json_type_object)) {
            continue;
        }
        if (dataset_entry_matches(entry, "1+1", "0.800", 0.8, 0.9, 0.05)) {
            reinforce_entry = 1;
        }
        if (dataset_entry_matches(entry,
                                  "practice addition",
                                  "0.420",
                                  0.4,
                                  0.37,
                                  0.0)) {
            interaction_entry = 1;
        }
    }
    assert(reinforce_entry);
    assert(interaction_entry);

    struct json_object *memory = NULL;
    assert(json_object_object_get_ex(root, "memory", &memory));
    assert(json_object_is_type(memory, json_type_object));
    struct json_object *facts = NULL;
    assert(json_object_object_get_ex(memory, "facts", &facts));
    assert(json_object_is_type(facts, json_type_array));
    int fact_found = 0;
    size_t fact_len = json_object_array_length(facts);
    for (size_t i = 0; i < fact_len; ++i) {
        struct json_object *fact = json_object_array_get_idx(facts, (int)i);
        if (!fact || !json_object_is_type(fact, json_type_object)) {
            continue;
        }
        if (memory_fact_matches(fact, "test.reinforce", "1+1", 0.8)) {
            fact_found = 1;
            break;
        }
    }
    assert(fact_found);

    json_object_put(root);
    free(roundtrip);
    kolibri_ai_destroy(clone);
    kolibri_ai_destroy(ai);
    return 0;
}

