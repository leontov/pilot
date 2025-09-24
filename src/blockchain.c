/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "blockchain.h"

#include "util/log.h"

#include <math.h>
#include <openssl/evp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char byte;

static size_t safe_strnlen(const char *s, size_t max_len) {
    size_t len = 0;
    if (!s) {
        return 0;
    }
    while (len < max_len && s[len] != '\0') {
        len++;
    }
    return len;
}

#define INITIAL_CAPACITY 16
#define DIFFICULTY_TARGET "000"
#define MAX_BLOCKCHAIN_SIZE 1000

static const double DEFAULT_MIN_POU = 0.6;
static const double DEFAULT_MAX_MDL_DELTA = 128.0;

static const char GENESIS_PREV_HASH[] =
    "0000000000000000000000000000000000000000000000000000000000000000";

static void update_audit_message(BlockchainLogMessage *message,
                                 const char *fmt,
                                 ...) {
    if (!message) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message->message, sizeof(message->message), fmt, ap);
    va_end(ap);
    message->timestamp = time(NULL);
}

static bool calculate_hash(const Block *block,
                           byte *digest_out,
                           unsigned int *digest_len_out,
                           char *hex_output) {
    if (!block) {
        return false;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return false;
    }

    const EVP_MD *md = EVP_sha256();
    if (EVP_DigestInit_ex(ctx, md, NULL) != 1) {
        EVP_MD_CTX_free(ctx);
        return false;
    }

    EVP_DigestUpdate(ctx,
                     block->prev_hash,
                     safe_strnlen(block->prev_hash, sizeof(block->prev_hash)));
    EVP_DigestUpdate(ctx, &block->timestamp, sizeof(time_t));
    EVP_DigestUpdate(ctx, &block->poe_threshold, sizeof(double));
    EVP_DigestUpdate(ctx, &block->mdl_delta, sizeof(double));

    for (size_t i = 0; i < block->formula_count; ++i) {
        const Formula *formula = block->formulas[i];
        if (!formula) {
            continue;
        }
        if (formula->representation == FORMULA_REPRESENTATION_ANALYTIC) {
            if (formula->expression) {
                EVP_DigestUpdate(ctx, formula->expression, strlen(formula->expression));
            }
            if (formula->coeff_count > 0 && formula->coefficients) {
                EVP_DigestUpdate(ctx,
                                 formula->coefficients,
                                 sizeof(double) * formula->coeff_count);
            }
        } else {
            EVP_DigestUpdate(ctx,
                             formula->content,
                             safe_strnlen(formula->content, sizeof(formula->content)));
        }
        EVP_DigestUpdate(ctx, &formula->effectiveness, sizeof(double));
    }

    EVP_DigestUpdate(ctx, &block->nonce, sizeof(uint32_t));

    byte digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return false;
    }

    if (digest_out && digest_len_out) {
        memcpy(digest_out, digest, digest_len);
        *digest_len_out = digest_len;
    }

    if (hex_output) {
        for (unsigned int i = 0; i < digest_len; ++i) {
            sprintf(hex_output + (i * 2), "%02x", digest[i]);
        }
        hex_output[digest_len * 2] = '\0';
    }

    EVP_MD_CTX_free(ctx);
    return true;
}

static void block_free(Block *block);
static void block_formula_clear(Formula *formula);
static int block_copy_formula(Formula *dest, const Formula *src);

static void ensure_capacity(Blockchain *chain) {
    if (!chain) {
        return;
    }
    if (chain->block_count < chain->capacity) {
        return;
    }
    size_t new_capacity = chain->capacity ? chain->capacity * 2 : INITIAL_CAPACITY;
    Block **new_blocks = realloc(chain->blocks, new_capacity * sizeof(Block *));
    if (!new_blocks) {
        log_error("[blockchain] failed to resize block array to %zu entries", new_capacity);
        return;
    }
    chain->blocks = new_blocks;
    chain->capacity = new_capacity;
}

static void reset_audit(Blockchain *chain) {
    if (!chain) {
        return;
    }
    memset(&chain->audit, 0, sizeof(chain->audit));
}

Blockchain *blockchain_create(void) {
    Blockchain *chain = calloc(1, sizeof(Blockchain));
    if (!chain) {
        return NULL;
    }
    chain->blocks = calloc(INITIAL_CAPACITY, sizeof(Block *));
    if (!chain->blocks) {
        free(chain);
        return NULL;
    }
    chain->capacity = INITIAL_CAPACITY;
    chain->block_count = 0;
    chain->main_tip_index = SIZE_MAX;
    chain->policy.min_pou_threshold = DEFAULT_MIN_POU;
    chain->policy.max_mdl_delta = DEFAULT_MAX_MDL_DELTA;
    reset_audit(chain);
    return chain;
}

void blockchain_destroy(Blockchain *chain) {
    if (!chain) {
        return;
    }
    for (size_t i = 0; i < chain->block_count; ++i) {
        block_free(chain->blocks[i]);
    }
    free(chain->blocks);
    free(chain);
}

void blockchain_set_policy(Blockchain *chain, double min_pou_threshold, double max_mdl_delta) {
    if (!chain) {
        return;
    }
    if (min_pou_threshold > 0.0) {
        chain->policy.min_pou_threshold = min_pou_threshold;
    }
    if (max_mdl_delta > 0.0) {
        chain->policy.max_mdl_delta = max_mdl_delta;
    }
}

double blockchain_score_formula(const Formula *formula, double *poe_out, double *mdl_out) {
    if (!formula) {
        if (poe_out) {
            *poe_out = 0.0;
        }
        if (mdl_out) {
            *mdl_out = 0.0;
        }
        return 0.0;
    }

    double poe = formula->effectiveness;
    if (poe < 0.0) {
        poe = 0.0;
    } else if (poe > 1.0) {
        poe = 1.0;
    }

    double mdl = 0.0;
    if (formula->representation == FORMULA_REPRESENTATION_TEXT) {
        mdl = (double)safe_strnlen(formula->content, sizeof(formula->content));
    } else if (formula->representation == FORMULA_REPRESENTATION_ANALYTIC) {
        if (formula->coefficients && formula->coeff_count > 0) {
            mdl += (double)formula->coeff_count * 4.0;
        }
        if (formula->expression) {
            mdl += (double)strlen(formula->expression);
        }
        mdl += 8.0;
    }

    if (mdl < 0.0) {
        mdl = 0.0;
    }

    if (poe_out) {
        *poe_out = poe;
    }
    if (mdl_out) {
        *mdl_out = mdl;
    }

    double score = poe - 0.01 * mdl;
    if (score < 0.0) {
        score = 0.0;
    }
    return score;
}

static Block *block_alloc(size_t formula_count) {
    Block *block = calloc(1, sizeof(Block));
    if (!block) {
        return NULL;
    }
    block->formulas = calloc(formula_count, sizeof(Formula *));
    if (!block->formulas) {
        free(block);
        return NULL;
    }
    block->owned_formulas = calloc(formula_count, sizeof(Formula));
    if (!block->owned_formulas) {
        free(block->formulas);
        free(block);
        return NULL;
    }
    block->formula_count = formula_count;
    block->parent_index = SIZE_MAX;
    block->height = 1;
    block->validation_status = BLOCK_VALIDATION_PENDING;
    return block;
}

static void block_free(Block *block) {
    if (!block) {
        return;
    }
    if (block->owned_formulas) {
        for (size_t i = 0; i < block->formula_count; ++i) {
            block_formula_clear(&block->owned_formulas[i]);
        }
        free(block->owned_formulas);
    }
    free(block->formulas);
    free(block);
}

static int block_copy_formula(Formula *dest, const Formula *src) {
    if (!dest) {
        return -1;
    }
    memset(dest, 0, sizeof(*dest));
    if (!src) {
        return 0;
    }

    memcpy(dest->id, src->id, sizeof(dest->id));
    dest->effectiveness = src->effectiveness;
    dest->created_at = src->created_at;
    dest->tests_passed = src->tests_passed;
    dest->confirmations = src->confirmations;
    dest->representation = src->representation;
    dest->type = src->type;
    strncpy(dest->content, src->content, sizeof(dest->content) - 1);
    dest->content[sizeof(dest->content) - 1] = '\0';

    if (src->representation == FORMULA_REPRESENTATION_ANALYTIC) {
        if (src->coeff_count > 0 && src->coefficients) {
            dest->coefficients = malloc(sizeof(double) * src->coeff_count);
            if (!dest->coefficients) {
                return -1;
            }
            memcpy(dest->coefficients, src->coefficients, sizeof(double) * src->coeff_count);
            dest->coeff_count = src->coeff_count;
        }
        if (src->expression) {
            size_t len = strlen(src->expression) + 1;
            dest->expression = malloc(len);
            if (!dest->expression) {
                free(dest->coefficients);
                dest->coefficients = NULL;
                dest->coeff_count = 0;
                return -1;
            }
            memcpy(dest->expression, src->expression, len);
        }
    }

    return 0;
}

static void block_formula_clear(Formula *formula) {
    if (!formula) {
        return;
    }
    free(formula->coefficients);
    formula->coefficients = NULL;
    formula->coeff_count = 0;
    free(formula->expression);
    formula->expression = NULL;
}

static bool block_clone_formulas(Block *block, Formula **formulas) {
    for (size_t i = 0; i < block->formula_count; ++i) {
        Formula *dest = &block->owned_formulas[i];
        Formula *src = formulas[i];
        if (src) {
            if (block_copy_formula(dest, src) != 0) {
                return false;
            }
            block->formulas[i] = dest;
        } else {
            block->formulas[i] = NULL;
        }
    }
    return true;
}

static void compute_block_statistics(Block *block) {
    double total_poe = 0.0;
    double total_mdl = 0.0;
    double total_score = 0.0;
    size_t sampled = 0;

    for (size_t i = 0; i < block->formula_count; ++i) {
        Formula *formula = block->formulas[i];
        if (!formula) {
            continue;
        }
        double formula_poe = 0.0;
        double formula_mdl = 0.0;
        double formula_score = blockchain_score_formula(formula, &formula_poe, &formula_mdl);
        total_poe += formula_poe;
        total_mdl += formula_mdl;
        total_score += formula_score;
        sampled++;
    }

    if (sampled > 0) {
        block->poe_sum = total_poe;
        block->mdl_sum = total_mdl;
        block->score_sum = total_score;
        block->poe_average = total_poe / (double)sampled;
        block->mdl_average = total_mdl / (double)sampled;
        block->score_average = total_score / (double)sampled;
    } else {
        block->poe_sum = 0.0;
        block->mdl_sum = 0.0;
        block->score_sum = 0.0;
        block->poe_average = 0.0;
        block->mdl_average = 0.0;
        block->score_average = 0.0;
    }
}

static size_t find_block_index_by_hash(const Blockchain *chain, const char *hash) {
    if (!chain || !hash) {
        return SIZE_MAX;
    }
    for (size_t i = 0; i < chain->block_count; ++i) {
        if (strcmp(chain->blocks[i]->hash, hash) == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}

static void rebuild_main_chain(Blockchain *chain, size_t tip_index) {
    if (!chain) {
        return;
    }
    for (size_t i = 0; i < chain->block_count; ++i) {
        chain->blocks[i]->on_main_chain = false;
    }
    size_t cursor = tip_index;
    while (cursor != SIZE_MAX) {
        Block *blk = chain->blocks[cursor];
        blk->on_main_chain = true;
        cursor = blk->parent_index;
    }
    chain->main_tip_index = tip_index;
    update_audit_message(&chain->audit.finalization,
                         "finalized tip %s height=%zu cumulative_poe=%.4f",
                         chain->blocks[tip_index]->hash,
                         chain->blocks[tip_index]->height,
                         chain->blocks[tip_index]->cumulative_poe);
    log_info("[blockchain] finalized new tip %s (height=%zu, cumulative_poe=%.4f)",
             chain->blocks[tip_index]->hash,
             chain->blocks[tip_index]->height,
             chain->blocks[tip_index]->cumulative_poe);
}

bool blockchain_add_block(Blockchain *chain,
                          const BlockchainBlockSpec *spec,
                          BlockValidationStatus *status_out) {
    if (!chain || !spec || !spec->formulas || spec->formula_count == 0) {
        return false;
    }
    if (chain->block_count >= MAX_BLOCKCHAIN_SIZE) {
        log_warn("[blockchain] size limit reached (%zu entries)", chain->block_count);
        if (status_out) {
            *status_out = BLOCK_VALIDATION_REJECTED;
        }
        return false;
    }

    Block *block = block_alloc(spec->formula_count);
    if (!block) {
        if (status_out) {
            *status_out = BLOCK_VALIDATION_REJECTED;
        }
        return false;
    }

    if (!block_clone_formulas(block, spec->formulas)) {
        block_free(block);
        if (status_out) {
            *status_out = BLOCK_VALIDATION_REJECTED;
        }
        return false;
    }
    compute_block_statistics(block);

    block->poe_threshold = spec->poe_threshold > 0.0 ? spec->poe_threshold
                                                     : chain->policy.min_pou_threshold;
    const double mdl_tolerance = spec->mdl_tolerance > 0.0 ? spec->mdl_tolerance
                                                           : chain->policy.max_mdl_delta;
    block->timestamp = spec->timestamp ? spec->timestamp : time(NULL);

    size_t parent_index = SIZE_MAX;
    const char *parent_hash = GENESIS_PREV_HASH;
    if (spec->prev_hash && spec->prev_hash[0] != '\0') {
        parent_index = find_block_index_by_hash(chain, spec->prev_hash);
        if (parent_index == SIZE_MAX && strcmp(spec->prev_hash, GENESIS_PREV_HASH) != 0) {
            log_warn("[blockchain] unknown parent hash %s", spec->prev_hash);
            update_audit_message(&chain->audit.verification,
                                 "rejected block (unknown parent %s)",
                                 spec->prev_hash);
            if (status_out) {
                *status_out = BLOCK_VALIDATION_REJECTED;
            }
            block_free(block);
            return false;
        }
    } else if (chain->main_tip_index != SIZE_MAX) {
        parent_index = chain->main_tip_index;
    }

    if (parent_index != SIZE_MAX) {
        Block *parent = chain->blocks[parent_index];
        block->parent_index = parent_index;
        block->height = parent->height + 1;
        parent_hash = parent->hash;
        block->mdl_delta = block->mdl_average - parent->mdl_average;
    } else {
        block->parent_index = SIZE_MAX;
        block->height = 1;
        block->mdl_delta = 0.0;
    }

    strncpy(block->prev_hash, parent_hash, sizeof(block->prev_hash) - 1);
    block->prev_hash[sizeof(block->prev_hash) - 1] = '\0';

    byte digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (spec->nonce) {
        block->nonce = spec->nonce;
        if (!calculate_hash(block, digest, &digest_len, block->hash)) {
            if (status_out) {
                *status_out = BLOCK_VALIDATION_REJECTED;
            }
            block_free(block);
            return false;
        }
    } else {
        block->nonce = 0;
        char hash_hex[65];
        do {
            block->nonce++;
            if (!calculate_hash(block, digest, &digest_len, hash_hex)) {
                if (status_out) {
                    *status_out = BLOCK_VALIDATION_REJECTED;
                }
                block_free(block);
                return false;
            }
        } while (strncmp(hash_hex, DIFFICULTY_TARGET, strlen(DIFFICULTY_TARGET)) != 0);
        memcpy(block->hash, hash_hex, sizeof(block->hash));
    }

    const bool difficulty_ok =
        strncmp(block->hash, DIFFICULTY_TARGET, strlen(DIFFICULTY_TARGET)) == 0;
    const bool poe_ok = block->poe_average + 1e-9 >= block->poe_threshold;
    const bool mdl_ok = fabs(block->mdl_delta) <= mdl_tolerance + 1e-9;

    if (!(difficulty_ok && poe_ok && mdl_ok)) {
        block->validation_status = BLOCK_VALIDATION_REJECTED;
        update_audit_message(&chain->audit.verification,
                             "rejected block %s (difficulty=%s, poe=%.3f/%.3f, mdl_delta=%.3f tol=%.3f)",
                             block->hash,
                             difficulty_ok ? "ok" : "fail",
                             block->poe_average,
                             block->poe_threshold,
                             block->mdl_delta,
                             mdl_tolerance);
        log_info("[blockchain] rejected block %s difficulty_ok=%d poe=%.3f threshold=%.3f mdl_delta=%.3f tol=%.3f",
                 block->hash,
                 difficulty_ok ? 1 : 0,
                 block->poe_average,
                 block->poe_threshold,
                 block->mdl_delta,
                 mdl_tolerance);
        if (status_out) {
            *status_out = BLOCK_VALIDATION_REJECTED;
        }
        block_free(block);
        return false;
    }

    block->validation_status = BLOCK_VALIDATION_ACCEPTED;
    block->cumulative_poe = block->poe_average;
    block->cumulative_score = block->score_average;
    if (block->parent_index != SIZE_MAX) {
        Block *parent = chain->blocks[block->parent_index];
        block->cumulative_poe += parent->cumulative_poe;
        block->cumulative_score += parent->cumulative_score;
    }

    ensure_capacity(chain);
    if (chain->block_count >= chain->capacity) {
        if (status_out) {
            *status_out = BLOCK_VALIDATION_REJECTED;
        }
        block_free(block);
        return false;
    }

    chain->blocks[chain->block_count] = block;
    size_t new_index = chain->block_count;
    chain->block_count++;

    const double current_best = (chain->main_tip_index != SIZE_MAX)
                                    ? chain->blocks[chain->main_tip_index]->cumulative_poe
                                    : -1.0;
    if (chain->main_tip_index == SIZE_MAX || block->cumulative_poe > current_best + 1e-9 ||
        (fabs(block->cumulative_poe - current_best) <= 1e-9 &&
         block->height > chain->blocks[chain->main_tip_index]->height)) {
        rebuild_main_chain(chain, new_index);
    }

    update_audit_message(&chain->audit.verification,
                         "validated block %s poe=%.3f mdl_delta=%.3f",
                         block->hash,
                         block->poe_average,
                         block->mdl_delta);
    log_info("[blockchain] accepted block %s (poe=%.3f, mdl_delta=%.3f, height=%zu)",
             block->hash,
             block->poe_average,
             block->mdl_delta,
             block->height);

    if (status_out) {
        *status_out = BLOCK_VALIDATION_ACCEPTED;
    }
    return true;
}

bool blockchain_verify(const Blockchain *chain) {
    if (!chain) {
        return false;
    }
    if (chain->main_tip_index == SIZE_MAX) {
        update_audit_message((BlockchainLogMessage *)&chain->audit.verification,
                             "verification skipped (empty chain)");
        return true;
    }

    size_t cursor = chain->main_tip_index;
    const double tolerance = chain->policy.max_mdl_delta;
    while (cursor != SIZE_MAX) {
        const Block *block = chain->blocks[cursor];
        byte digest[EVP_MAX_MD_SIZE];
        unsigned int digest_len = 0;
        char expected_hash[65];
        if (!calculate_hash(block, digest, &digest_len, expected_hash)) {
            return false;
        }
        if (strcmp(expected_hash, block->hash) != 0) {
            return false;
        }
        if (strncmp(block->hash, DIFFICULTY_TARGET, strlen(DIFFICULTY_TARGET)) != 0) {
            return false;
        }
        if (block->validation_status != BLOCK_VALIDATION_ACCEPTED) {
            return false;
        }
        if (block->poe_average + 1e-9 < block->poe_threshold) {
            return false;
        }
        if (fabs(block->mdl_delta) > tolerance + 1e-9) {
            return false;
        }
        if (block->parent_index == SIZE_MAX) {
            if (strcmp(block->prev_hash, GENESIS_PREV_HASH) != 0) {
                return false;
            }
        } else {
            const Block *parent = chain->blocks[block->parent_index];
            if (strcmp(block->prev_hash, parent->hash) != 0) {
                return false;
            }
        }
        cursor = block->parent_index;
    }

    update_audit_message((BlockchainLogMessage *)&chain->audit.verification,
                         "verification passed height=%zu tip=%s",
                         chain->blocks[chain->main_tip_index]->height,
                         chain->blocks[chain->main_tip_index]->hash);
    log_info("[blockchain] verification passed tip=%s height=%zu",
             chain->blocks[chain->main_tip_index]->hash,
             chain->blocks[chain->main_tip_index]->height);
    return true;
}

const char *blockchain_get_last_hash(const Blockchain *chain) {
    if (!chain || chain->main_tip_index == SIZE_MAX) {
        return GENESIS_PREV_HASH;
    }
    return chain->blocks[chain->main_tip_index]->hash;
}

size_t blockchain_height(const Blockchain *chain) {
    if (!chain || chain->main_tip_index == SIZE_MAX) {
        return 0;
    }
    return chain->blocks[chain->main_tip_index]->height;
}

const Block *blockchain_find_block(const Blockchain *chain, const char *hash) {
    size_t idx = find_block_index_by_hash(chain, hash);
    if (idx == SIZE_MAX) {
        return NULL;
    }
    return chain->blocks[idx];
}
