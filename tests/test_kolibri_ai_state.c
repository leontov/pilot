#include "kolibri_ai.h"

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
    KolibriAI *ai = kolibri_ai_create(&cfg);
    assert(ai != NULL);

    kolibri_ai_start(ai);
    struct timespec ts = {0, 100000 * 1000};
    nanosleep(&ts, NULL);
    kolibri_ai_stop(ai);

    char *state = kolibri_ai_serialize_state(ai);
    assert(state != NULL);
    ensure_contains(state, "\"iterations\"");
    ensure_contains(state, "\"formula_count\"");
    free(state);

    char *formulas = kolibri_ai_serialize_formulas(ai, 3);
    assert(formulas != NULL);
    ensure_contains(formulas, "formulas");
    ensure_contains(formulas, "kolibri");
    free(formulas);

    kolibri_ai_destroy(ai);
    return 0;
}
