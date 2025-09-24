/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blockchain.h"

static void test_blockchain_verifier_detects_tampering(void) {
    Blockchain* chain = blockchain_create();
    assert(chain);

    Formula formula1;
    Formula formula2;
    memset(&formula1, 0, sizeof(formula1));
    memset(&formula2, 0, sizeof(formula2));
    strncpy(formula1.id, "formula_001", sizeof(formula1.id) - 1);
    strncpy(formula2.id, "formula_002", sizeof(formula2.id) - 1);
    strncpy(formula1.content, "payload_one", sizeof(formula1.content) - 1);
    strncpy(formula2.content, "payload_two", sizeof(formula2.content) - 1);
    formula1.representation = FORMULA_REPRESENTATION_TEXT;
    formula2.representation = FORMULA_REPRESENTATION_TEXT;
    formula1.effectiveness = 0.9;
    formula2.effectiveness = 0.9;

    Formula* block1_formulas[] = {&formula1};
    Formula* block2_formulas[] = {&formula2};

    BlockchainBlockSpec spec1 = {.formulas = block1_formulas, .formula_count = 1};
    BlockchainBlockSpec spec2 = {.formulas = block2_formulas, .formula_count = 1};
    BlockValidationStatus status = BLOCK_VALIDATION_PENDING;

    assert(blockchain_add_block(chain, &spec1, &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);
    status = BLOCK_VALIDATION_PENDING;
    assert(blockchain_add_block(chain, &spec2, &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);
    assert(chain->block_count == 2);

    assert(blockchain_verify(chain));

    Block* last_block = chain->blocks[chain->block_count - 1];
    last_block->nonce += 1;
    last_block->formulas[0]->content[0] = 'X';

    assert(!blockchain_verify(chain));

    blockchain_destroy(chain);
}

int main(void) {
    test_blockchain_verifier_detects_tampering();
    printf("Blockchain verifier regression test passed.\n");
    return 0;
}

