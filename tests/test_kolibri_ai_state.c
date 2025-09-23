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
    KolibriAI *ai = kolibri_ai_create();
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

    kolibri_ai_destroy(ai);
    return 0;
}
