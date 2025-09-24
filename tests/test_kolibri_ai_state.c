/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "kolibri_ai.h"
#include "formula.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void ensure_contains(const char *json, const char *needle) {
    if (!json || !needle) {
        assert(0 && "invalid input");
    }
    if (!strstr(json, needle)) {
        fprintf(stderr, "expected '%s' in '%s'\n", needle, json);
        assert(0 && "missing field");
    }
}

int main(void) {
    kolibri_config_t cfg = {0};
    cfg.search = formula_search_config_default();
    cfg.search.max_candidates = 2;
    KolibriAI *empty = kolibri_ai_create(&cfg);
    assert(empty != NULL);
    char *empty_snapshot = kolibri_ai_export_snapshot(empty);
    assert(empty_snapshot != NULL);
    ensure_contains(empty_snapshot, "\"dataset\":[]");
    ensure_contains(empty_snapshot, "\"memory\":[]");

    KolibriAI *empty_target = kolibri_ai_create(&cfg);
    assert(empty_target != NULL);
    assert(kolibri_ai_import_snapshot(empty_target, empty_snapshot) == 0);
    char *empty_roundtrip = kolibri_ai_export_snapshot(empty_target);
    assert(empty_roundtrip != NULL);
    ensure_contains(empty_roundtrip, "\"dataset\":[]");
    ensure_contains(empty_roundtrip, "\"memory\":[]");
    free(empty_roundtrip);
    kolibri_ai_destroy(empty_target);
    free(empty_snapshot);
    kolibri_ai_destroy(empty);

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

    char *state = kolibri_ai_serialize_state(ai);
    assert(state != NULL);
    ensure_contains(state, "\"iterations\"");
    ensure_contains(state, "\"formula_count\"");
    ensure_contains(state, "\"planning_score\"");
    ensure_contains(state, "\"recent_poe\"");
    ensure_contains(state, "\"recent_mdl\"");
    free(state);

    char *formulas = kolibri_ai_serialize_formulas(ai, 3);
    assert(formulas != NULL);
    ensure_contains(formulas, "formulas");
    ensure_contains(formulas, "kolibri");
    free(formulas);

    char *snapshot = kolibri_ai_export_snapshot(ai);
    assert(snapshot != NULL);
    ensure_contains(snapshot, "\"dataset\"");
    ensure_contains(snapshot, "\"memory\"");
    ensure_contains(snapshot, "\"prompt\"");
    ensure_contains(snapshot, "\"key\"");

    KolibriAI *restored = kolibri_ai_create(&cfg);
    assert(restored != NULL);
    assert(kolibri_ai_import_snapshot(restored, snapshot) == 0);
    char *restored_snapshot = kolibri_ai_export_snapshot(restored);
    assert(restored_snapshot != NULL);
    ensure_contains(restored_snapshot, "\"dataset\"");
    ensure_contains(restored_snapshot, "\"memory\"");
    free(restored_snapshot);
    kolibri_ai_destroy(restored);
    free(snapshot);

    kolibri_ai_destroy(ai);
    return 0;
}
