#include "blockchain.h"
#include "formula.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void init_text_formula(Formula *formula, const char *id, const char *content, double poe) {
    memset(formula, 0, sizeof(*formula));
    strncpy(formula->id, id, sizeof(formula->id) - 1);
    strncpy(formula->content, content, sizeof(formula->content) - 1);
    formula->effectiveness = poe;
    formula->created_at = time(NULL);
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    formula->type = FORMULA_COMPOSITE;
}

static void test_blockchain_poe_threshold(void) {
    Blockchain *chain = blockchain_create();
    assert(chain);

    Formula first;
    Formula second;
    init_text_formula(&first, "form-001", "payload-one", 0.92);
    init_text_formula(&second, "form-002", "payload-two", 0.86);

    Formula *formulas[] = {&first, &second};
    assert(blockchain_add_block(chain, formulas, 2));
    assert(chain->block_count == 1);
    assert(blockchain_verify(chain));

    Block *block = chain->blocks[0];
    assert(block);
    assert(block->poe_average >= 0.88 - 1e-6);

    Formula low;
    init_text_formula(&low, "form-003", "payload-low", 0.45);
    Formula *low_list[] = {&low};
    assert(!blockchain_add_block(chain, low_list, 1));
    assert(chain->block_count == 1);

    blockchain_destroy(chain);
}

static void test_blockchain_sync_replication(void) {
    Blockchain *source = blockchain_create();
    Blockchain *replica = blockchain_create();
    assert(source && replica);

    Formula first;
    Formula second;
    init_text_formula(&first, "sync-001", "sync-one", 0.95);
    init_text_formula(&second, "sync-002", "sync-two", 0.87);
    Formula *formulas[] = {&first, &second};
    assert(blockchain_add_block(source, formulas, 2));

    Formula third;
    init_text_formula(&third, "sync-003", "sync-three", 0.9);
    Formula *second_block[] = {&third};
    assert(blockchain_add_block(source, second_block, 1));

    assert(source->block_count == 2);
    assert(blockchain_verify(source));

    int appended = blockchain_sync(replica, source);
    assert(appended == 2);
    assert(replica->block_count == source->block_count);
    assert(blockchain_verify(replica));

    blockchain_destroy(source);
    blockchain_destroy(replica);
}

int main(void) {
    test_blockchain_poe_threshold();
    test_blockchain_sync_replication();
    printf("Blockchain storage consensus tests passed.\n");
    return 0;
}
