/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_BLOCKCHAIN_H
#define KOLIBRI_BLOCKCHAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "formula_advanced.h"

typedef enum {
    BLOCK_VALIDATION_PENDING = 0,
    BLOCK_VALIDATION_ACCEPTED = 1,
    BLOCK_VALIDATION_REJECTED = 2
} BlockValidationStatus;

typedef struct {
    double min_pou_threshold;
    double max_mdl_delta;
} BlockchainPolicy;

typedef struct {
    char message[256];
    time_t timestamp;
} BlockchainLogMessage;

typedef struct {
    BlockchainLogMessage verification;
    BlockchainLogMessage finalization;
} BlockchainAuditLog;

typedef struct {
    Formula **formulas;
    size_t formula_count;
    const char *prev_hash;
    double poe_threshold;
    double mdl_tolerance;
    time_t timestamp;
    uint32_t nonce;
} BlockchainBlockSpec;

typedef struct Block {
    Formula **formulas;
    Formula *owned_formulas;
    size_t formula_count;
    char prev_hash[65];
    char hash[65];
    time_t timestamp;
    uint32_t nonce;
    double poe_sum;
    double poe_average;
    double mdl_sum;
    double mdl_average;
    double score_sum;
    double score_average;
    double poe_threshold;
    double mdl_delta;
    double cumulative_poe;
    double cumulative_score;
    size_t parent_index;
    size_t height;
    bool on_main_chain;
    BlockValidationStatus validation_status;
} Block;

typedef struct {
    Block **blocks;
    size_t block_count;
    size_t capacity;
    size_t main_tip_index;
    BlockchainPolicy policy;
    BlockchainAuditLog audit;
} Blockchain;

Blockchain *blockchain_create(void);
void blockchain_destroy(Blockchain *chain);

void blockchain_set_policy(Blockchain *chain, double min_pou_threshold, double max_mdl_delta);

bool blockchain_add_block(Blockchain *chain,
                          const BlockchainBlockSpec *spec,
                          BlockValidationStatus *status_out);

bool blockchain_verify(const Blockchain *chain);

const char *blockchain_get_last_hash(const Blockchain *chain);
size_t blockchain_height(const Blockchain *chain);

const Block *blockchain_find_block(const Blockchain *chain, const char *hash);

double blockchain_score_formula(const Formula *formula, double *poe_out, double *mdl_out);

static inline const BlockchainAuditLog *blockchain_get_audit(const Blockchain *chain) {
    return chain ? &chain->audit : NULL;
}

#endif /* KOLIBRI_BLOCKCHAIN_H */
