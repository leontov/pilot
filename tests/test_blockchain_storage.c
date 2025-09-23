#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "blockchain.h"
#include "formula.h"

static void init_text_formula(Formula* formula, const char* id, const char* content) {
    memset(formula, 0, sizeof(*formula));
    strncpy(formula->id, id, sizeof(formula->id) - 1);
    formula->effectiveness = 0.42;
    formula->created_at = time(NULL);
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    strncpy(formula->content, content, sizeof(formula->content) - 1);
}

static void test_blockchain_stores_deep_copies(void) {
    Blockchain* chain = blockchain_create();
    assert(chain);

    Formula original;
    init_text_formula(&original, "formula_001", "original payload");

    Formula* formulas[] = { &original };
    assert(blockchain_add_block(chain, formulas, 1));
    assert(chain->block_count == 1);

    Block* stored_block = chain->blocks[0];
    assert(stored_block);
    assert(stored_block->formula_count == 1);

    Formula* stored_formula = stored_block->formulas[0];
    assert(stored_formula);
    assert(stored_formula != &original);
    assert(strcmp(stored_formula->content, "original payload") == 0);

    char initial_hash[65];
    const char* hash_before = blockchain_get_last_hash(chain);
    strncpy(initial_hash, hash_before, sizeof(initial_hash));
    initial_hash[sizeof(initial_hash) - 1] = '\0';

    // Mutate the original formula after it has been added to the chain.
    strncpy(original.content, "mutated payload", sizeof(original.content) - 1);
    original.effectiveness = 0.99;

    const char* hash_after = blockchain_get_last_hash(chain);
    assert(strcmp(initial_hash, hash_after) == 0);
    assert(strcmp(stored_formula->content, "original payload") == 0);
    assert(stored_formula->effectiveness == 0.42);

    blockchain_destroy(chain);
}

int main(void) {
    test_blockchain_stores_deep_copies();
    printf("Blockchain storage deep copy test passed.\n");
    return 0;
}
