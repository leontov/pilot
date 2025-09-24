/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockchain.h"

static Formula* create_text_formula(const char* id, const char* content) {
    Formula* formula = (Formula*)calloc(1, sizeof(Formula));
    assert(formula);

    strncpy(formula->id, id, sizeof(formula->id) - 1);
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    strncpy(formula->content, content, sizeof(formula->content) - 1);

    return formula;
}

static void free_formula(Formula* formula) {
    free(formula);
}

static void test_blockchain_verifier_detects_tampering(void) {
    Blockchain* chain = blockchain_create();
    assert(chain);

    Formula* formula1 = create_text_formula("formula_001", "payload_one");
    Formula* formula2 = create_text_formula("formula_002", "payload_two");

    Formula* block1_formulas[] = {formula1};
    Formula* block2_formulas[] = {formula2};

    assert(blockchain_add_block(chain, block1_formulas, 1));
    assert(blockchain_add_block(chain, block2_formulas, 1));
    assert(chain->block_count == 2);

    assert(blockchain_verify(chain));

    Block* last_block = chain->blocks[chain->block_count - 1];
    last_block->nonce += 1;
    last_block->formulas[0]->content[0] = 'X';

    assert(!blockchain_verify(chain));

    blockchain_destroy(chain);
    free_formula(formula1);
    free_formula(formula2);
}

int main(void) {
    test_blockchain_verifier_detects_tampering();
    printf("Blockchain verifier regression test passed.\n");
    return 0;
}

