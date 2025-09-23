#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "blockchain.h"
#include "formula.h"

static void init_text_formula(Formula* formula, const char* id, const char* content) {
    memset(formula, 0, sizeof(*formula));
    snprintf(formula->id, sizeof(formula->id), "%s", id);
    formula->effectiveness = 0.42;
    formula->created_at = time(NULL);
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    snprintf(formula->content, sizeof(formula->content), "%s", content);
}

int main(void) {
    Blockchain* chain = blockchain_create();
    assert(chain);

    Formula source;
    init_text_formula(&source, "source_formula", "original_payload");

    Formula* formulas[] = {&source};
    assert(blockchain_add_block(chain, formulas, 1));

    const char* initial_hash = blockchain_get_last_hash(chain);
    char stored_hash[65];
    strncpy(stored_hash, initial_hash, sizeof(stored_hash));
    stored_hash[sizeof(stored_hash) - 1] = '\0';

    Block* stored_block = chain->blocks[chain->block_count - 1];
    assert(stored_block);
    assert(stored_block->formula_count == 1);
    assert(stored_block->formulas[0]);

    char stored_content[sizeof(stored_block->formulas[0]->content)];
    strncpy(stored_content, stored_block->formulas[0]->content, sizeof(stored_content));
    stored_content[sizeof(stored_content) - 1] = '\0';

    source.effectiveness = 0.99;
    snprintf(source.content, sizeof(source.content), "%s", "mutated_payload");

    const char* after_hash = blockchain_get_last_hash(chain);
    assert(strcmp(stored_hash, after_hash) == 0);
    assert(strcmp(stored_block->formulas[0]->content, stored_content) == 0);

    blockchain_destroy(chain);

    printf("Blockchain deep copy regression test passed.\n");
    return 0;
}
