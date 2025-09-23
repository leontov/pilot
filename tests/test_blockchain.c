#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "kovian_blockchain.h"

static void init_formula(Formula* formula, double effectiveness, int index) {
    memset(formula, 0, sizeof(*formula));
    snprintf(formula->id, sizeof(formula->id), "formula_%03d", index);
    formula->effectiveness = effectiveness;
    formula->created_at = time(NULL);
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    snprintf(formula->content, sizeof(formula->content), "content_%03d", index);
}

static void build_chain(KovianChain* chain, size_t total_blocks, double low_effectiveness,
                        double high_effectiveness, size_t high_tail) {
    assert(chain);
    assert(total_blocks >= high_tail);

    Formula formula;
    for (size_t i = 0; i < total_blocks; ++i) {
        double effectiveness = (i < total_blocks - high_tail) ? low_effectiveness : high_effectiveness;
        init_formula(&formula, effectiveness, (int)i);
        assert(kovian_chain_add_block(chain, &formula, 1) != NULL);
    }
    assert(chain->length == total_blocks);
}

static void test_adjust_difficulty_increase(void) {
    KovianChain* chain = kovian_chain_create();
    assert(chain);

    build_chain(chain, 110, 0.5, 0.9, 100);

    double initial_difficulty = chain->difficulty;
    adjust_chain_difficulty(chain);

    double expected = initial_difficulty * 1.1;
    assert(fabs(chain->difficulty - expected) < 1e-9);

    kovian_chain_destroy(chain);
}

static void test_adjust_difficulty_decrease(void) {
    KovianChain* chain = kovian_chain_create();
    assert(chain);

    build_chain(chain, 120, 0.95, 0.3, 100);

    double initial_difficulty = chain->difficulty;
    adjust_chain_difficulty(chain);

    double expected = initial_difficulty * 0.9;
    assert(fabs(chain->difficulty - expected) < 1e-9);

    kovian_chain_destroy(chain);
}

int main(void) {
    test_adjust_difficulty_increase();
    test_adjust_difficulty_decrease();
    printf("All blockchain difficulty adjustment tests passed.\n");
    return 0;
}
