#include "blockchain.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void setup_formula(Formula *formula, const char *id, const char *content, double effectiveness) {
    memset(formula, 0, sizeof(*formula));
    strncpy(formula->id, id, sizeof(formula->id) - 1);
    strncpy(formula->content, content, sizeof(formula->content) - 1);
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    formula->effectiveness = effectiveness;
}

static void test_accepts_block_meeting_thresholds(void) {
    Blockchain *chain = blockchain_create();
    assert(chain);
    blockchain_set_policy(chain, 0.7, 48.0);

    Formula formula;
    setup_formula(&formula, "f_accept", "good_payload", 0.82);
    Formula *formulas[] = {&formula};

    BlockchainBlockSpec spec = {
        .formulas = formulas,
        .formula_count = 1,
    };
    BlockValidationStatus status = BLOCK_VALIDATION_PENDING;
    assert(blockchain_add_block(chain, &spec, &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);
    assert(blockchain_height(chain) == 1);

    const char *tip_hash = blockchain_get_last_hash(chain);
    const Block *tip = blockchain_find_block(chain, tip_hash);
    assert(tip);
    assert(tip->poe_average >= tip->poe_threshold);
    assert(fabs(tip->mdl_delta) < 1e-9);

    const BlockchainAuditLog *audit = blockchain_get_audit(chain);
    assert(audit);
    assert(audit->verification.message[0] != '\0');
    assert(audit->finalization.message[0] != '\0');

    blockchain_destroy(chain);
}

static void test_rejects_low_pou_block(void) {
    Blockchain *chain = blockchain_create();
    assert(chain);
    blockchain_set_policy(chain, 0.75, 64.0);

    Formula strong;
    setup_formula(&strong, "f_strong", "anchor", 0.9);
    Formula *anchor_formulas[] = {&strong};
    BlockchainBlockSpec anchor_spec = {.formulas = anchor_formulas, .formula_count = 1};
    BlockValidationStatus status = BLOCK_VALIDATION_PENDING;
    assert(blockchain_add_block(chain, &anchor_spec, &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);

    Formula weak;
    setup_formula(&weak, "f_weak", "too_weak", 0.2);
    Formula *weak_formulas[] = {&weak};
    BlockchainBlockSpec weak_spec = {
        .formulas = weak_formulas,
        .formula_count = 1,
        .prev_hash = blockchain_get_last_hash(chain),
    };
    status = BLOCK_VALIDATION_PENDING;
    assert(!blockchain_add_block(chain, &weak_spec, &status));
    assert(status == BLOCK_VALIDATION_REJECTED);
    assert(blockchain_height(chain) == 1);

    blockchain_destroy(chain);
}

static void test_rejects_excessive_mdl_delta(void) {
    Blockchain *chain = blockchain_create();
    assert(chain);
    blockchain_set_policy(chain, 0.6, 20.0);

    Formula baseline;
    setup_formula(&baseline, "f_base", "short", 0.8);
    Formula *base_formulas[] = {&baseline};
    BlockchainBlockSpec base_spec = {.formulas = base_formulas, .formula_count = 1};
    BlockValidationStatus status = BLOCK_VALIDATION_PENDING;
    assert(blockchain_add_block(chain, &base_spec, &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);

    Formula heavy;
    setup_formula(&heavy, "f_heavy", "abcdefghijklmnopqrstuvwxyz0123456789", 0.85);
    Formula *heavy_formulas[] = {&heavy};
    BlockchainBlockSpec heavy_spec = {
        .formulas = heavy_formulas,
        .formula_count = 1,
        .prev_hash = blockchain_get_last_hash(chain),
    };
    status = BLOCK_VALIDATION_PENDING;
    assert(!blockchain_add_block(chain, &heavy_spec, &status));
    assert(status == BLOCK_VALIDATION_REJECTED);
    assert(blockchain_height(chain) == 1);

    const BlockchainAuditLog *audit = blockchain_get_audit(chain);
    assert(audit && strstr(audit->verification.message, "rejected block"));

    blockchain_destroy(chain);
}

int main(void) {
    test_accepts_block_meeting_thresholds();
    test_rejects_low_pou_block();
    test_rejects_excessive_mdl_delta();
    printf("blockchain unit tests passed\n");
    return 0;
}
