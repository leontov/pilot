/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "blockchain.h"

static void init_formula(Formula* formula, int index) {
    memset(formula, 0, sizeof(*formula));
    snprintf(formula->id, sizeof(formula->id), "formula_%03d", index);
    formula->created_at = time(NULL);
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    snprintf(formula->content, sizeof(formula->content), "payload_%03d", index);
    formula->effectiveness = 0.85;
}

static void test_verify_rejects_tampered_tip(void) {
    Blockchain* chain = blockchain_create();
    assert(chain);

    Formula first_formula;
    Formula second_formula;
    Formula third_formula;
    Formula* block_formulas[1];
    BlockchainBlockSpec spec = {
        .formulas = block_formulas,
        .formula_count = 1,
    };
    BlockValidationStatus status = BLOCK_VALIDATION_PENDING;

    init_formula(&first_formula, 0);
    block_formulas[0] = &first_formula;
    block_formulas[0] = &first_formula;
    assert(blockchain_add_block(chain, &spec, &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);

    init_formula(&second_formula, 1);
    block_formulas[0] = &second_formula;
    status = BLOCK_VALIDATION_PENDING;
    assert(blockchain_add_block(chain, &spec, &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);

    init_formula(&third_formula, 2);
    block_formulas[0] = &third_formula;
    status = BLOCK_VALIDATION_PENDING;
    assert(blockchain_add_block(chain, &spec, &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);

    assert(chain->block_count == 3);
    assert(blockchain_verify(chain));

    Block* last_block = chain->blocks[chain->block_count - 1];
    assert(last_block);

    last_block->nonce += 1;  // Tamper with the mining result
    last_block->formulas[0]->content[0] ^= 0x1;  // Mutate payload

    assert(!blockchain_verify(chain));

    blockchain_destroy(chain);
}

int main(void) {
    test_verify_rejects_tampered_tip();
    printf("Blockchain verification tampering test passed.\n");
    return 0;
}
