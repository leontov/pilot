/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "blockchain.h"
#include "formula.h"
#include "util/key_manager.h"
#include "util/log.h"
#include <ctype.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#define DIFFICULTY_TARGET "000" // Первые три символа должны быть нулями
#define MAX_BLOCKCHAIN_SIZE 1000 // Максимальный размер блокчейна

static const char GENESIS_PREV_HASH[] =
    "0000000000000000000000000000000000000000000000000000000000000000";

static key_file_t g_private_key = {0};
static key_file_t g_public_key = {0};
static int g_security_enabled = 0;
static char g_signer_id[SWARM_NODE_ID_DIGITS + 1] = {0};

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static void log_evp_error(const char *context) {
    unsigned long err = 0;
    while ((err = ERR_get_error()) != 0) {
        log_error("%s: %s", context, ERR_error_string(err, NULL));
    }
}

static int parse_hex_key(const unsigned char *data, size_t len, unsigned char *out, size_t expected_len) {
    if (!data || !out) {
        return -1;
    }
    size_t count = 0;
    char cleaned[256];
    if (expected_len * 2 > sizeof(cleaned)) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = data[i];
        if (ch == '\0') {
            break;
        }
        if (isspace((unsigned char)ch)) {
            continue;
        }
        if (!isxdigit(ch)) {
            return -1;
        }
        if (count >= expected_len * 2) {
            return -1;
        }
        cleaned[count++] = (char)ch;
    }
    if (count != expected_len * 2) {
        return -1;
    }
    for (size_t i = 0; i < expected_len; ++i) {
        int hi = hex_value(cleaned[i * 2]);
        int lo = hex_value(cleaned[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}

static int hex_string_to_bytes(const char *hex, unsigned char *out, size_t out_len) {
    if (!hex || !out) {
        return -1;
    }
    for (size_t i = 0; i < out_len; ++i) {
        int hi = hex_value(hex[i * 2]);
        int lo = hex_value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}

static int digits_only_str(const char *str, size_t len) {
    if (!str) {
        return 0;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!isdigit((unsigned char)str[i])) {
            return 0;
        }
    }
    return 1;
}

static int load_private_key(unsigned char *out, size_t expected_len) {
    const unsigned char *data = NULL;
    size_t len = 0;
    if (key_file_get(&g_private_key, &data, &len) != 0) {
        log_error("blockchain: unable to load private key");
        return -1;
    }
    return parse_hex_key(data, len, out, expected_len);
}

static int load_public_key(unsigned char *out, size_t expected_len) {
    const unsigned char *data = NULL;
    size_t len = 0;
    if (key_file_get(&g_public_key, &data, &len) != 0) {
        log_error("blockchain: unable to load public key");
        return -1;
    }
    return parse_hex_key(data, len, out, expected_len);
}

int blockchain_security_init(const char *signer_id,
                             const char *private_key_path,
                             const char *public_key_path,
                             uint32_t rotation_interval_sec) {
    if (!signer_id || !private_key_path || !public_key_path) {
        return -1;
    }
    if (!digits_only_str(signer_id, SWARM_NODE_ID_DIGITS)) {
        log_error("blockchain: signer id must be %u digits", SWARM_NODE_ID_DIGITS);
        return -1;
    }
    if (g_security_enabled) {
        key_file_deinit(&g_private_key);
        key_file_deinit(&g_public_key);
        g_security_enabled = 0;
    }
    if (key_file_init(&g_private_key, private_key_path, rotation_interval_sec) != 0) {
        log_error("blockchain: failed to initialize private key manager");
        return -1;
    }
    if (key_file_init(&g_public_key, public_key_path, rotation_interval_sec) != 0) {
        key_file_deinit(&g_private_key);
        log_error("blockchain: failed to initialize public key manager");
        return -1;
    }
    memcpy(g_signer_id, signer_id, SWARM_NODE_ID_DIGITS);
    g_signer_id[SWARM_NODE_ID_DIGITS] = '\0';
    g_security_enabled = 1;
    return 0;
}

void blockchain_security_shutdown(void) {
    if (!g_security_enabled) {
        return;
    }
    key_file_deinit(&g_private_key);
    key_file_deinit(&g_public_key);
    g_security_enabled = 0;
    g_signer_id[0] = '\0';
}

static int blockchain_sign_block(Block *block, const char *hash_hex) {
    if (!g_security_enabled) {
        return 0;
    }
    unsigned char priv[32];
    if (load_private_key(priv, sizeof(priv)) != 0) {
        return -1;
    }
    unsigned char hash_bytes[32];
    if (hex_string_to_bytes(hash_hex, hash_bytes, sizeof(hash_bytes)) != 0) {
        return -1;
    }
    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, priv, sizeof(priv));
    if (!pkey) {
        log_evp_error("blockchain: failed to load private key");
        return -1;
    }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return -1;
    }
    int rc = 0;
    size_t sig_len = sizeof(block->signature);
    if (EVP_DigestSignInit(ctx, NULL, NULL, NULL, pkey) != 1) {
        log_evp_error("blockchain: DigestSignInit failed");
        rc = -1;
        goto done;
    }
    if (EVP_DigestSign(ctx,
                        block->signature,
                        &sig_len,
                        hash_bytes,
                        sizeof(hash_bytes)) != 1) {
        log_evp_error("blockchain: DigestSign failed");
        rc = -1;
        goto done;
    }
    block->signature_len = sig_len;
    memcpy(block->signer_id, g_signer_id, SWARM_NODE_ID_DIGITS + 1);

done:
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc;
}

static int blockchain_verify_signature(const Block *block, const char *hash_hex) {
    if (!g_security_enabled) {
        return 1;
    }
    if (!block || block->signature_len == 0) {
        return 0;
    }
    if (strncmp(block->signer_id, g_signer_id, SWARM_NODE_ID_DIGITS) != 0) {
        log_error("blockchain: unexpected signer %s", block->signer_id);
        return 0;
    }
    unsigned char pub[32];
    if (load_public_key(pub, sizeof(pub)) != 0) {
        return 0;
    }
    unsigned char hash_bytes[32];
    if (hex_string_to_bytes(hash_hex, hash_bytes, sizeof(hash_bytes)) != 0) {
        return 0;
    }
    EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, pub, sizeof(pub));
    if (!pkey) {
        log_evp_error("blockchain: failed to load public key");
        return 0;
    }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        EVP_PKEY_free(pkey);
        return 0;
    }
    int rc = 0;
    if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) != 1) {
        log_evp_error("blockchain: DigestVerifyInit failed");
        rc = 0;
        goto done;
    }
    if (EVP_DigestVerify(ctx,
                          block->signature,
                          block->signature_len,
                          hash_bytes,
                          sizeof(hash_bytes)) != 1) {
        log_evp_error("blockchain: signature verification failed");
        rc = 0;
        goto done;
    }
    rc = 1;

done:
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return rc;
}

// Обновление функции calculate_hash для использования EVP API
static void calculate_hash(const Block* block, char* output) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    const EVP_MD* md = EVP_sha256();

    EVP_DigestInit_ex(ctx, md, NULL);

    // Хэшируем предыдущий хэш
    EVP_DigestUpdate(ctx, block->prev_hash, strlen(block->prev_hash));

    // Хэшируем временную метку
    EVP_DigestUpdate(ctx, &block->timestamp, sizeof(time_t));

    // Хэшируем формулы
    for (size_t i = 0; i < block->formula_count; i++) {
        Formula* formula = block->formulas[i];
        if (!formula) {
            continue;
        }

        if (formula->representation == FORMULA_REPRESENTATION_ANALYTIC) {
            if (formula->expression) {
                EVP_DigestUpdate(ctx, formula->expression,
                                 strlen(formula->expression));
            }
            if (formula->coeff_count > 0 && formula->coefficients) {
                EVP_DigestUpdate(ctx, formula->coefficients,
                                 sizeof(double) * formula->coeff_count);
            }
        } else {
            EVP_DigestUpdate(ctx, formula->content,
                             safe_strnlen(formula->content, sizeof(formula->content)));
        }
    }

    // Хэшируем nonce
    EVP_DigestUpdate(ctx, &block->nonce, sizeof(uint32_t));

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);

    // Конвертируем в hex-строку
    for (unsigned int i = 0; i < hash_len; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[hash_len * 2] = '\0';

    EVP_MD_CTX_free(ctx);
}

double blockchain_score_formula(const Formula* formula, double* poe_out, double* mdl_out) {
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
        mdl += 8.0; // базовая стоимость аналитической формулы
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

Blockchain* blockchain_create(void) {
    Blockchain* chain = (Blockchain*)malloc(sizeof(Blockchain));
    if (!chain) return NULL;
    
    chain->blocks = (Block**)malloc(sizeof(Block*) * INITIAL_CAPACITY);
    if (!chain->blocks) {
        free(chain);
        return NULL;
    }
    
    chain->block_count = 0;
    chain->capacity = INITIAL_CAPACITY;
    
    return chain;
}

bool blockchain_add_block(Blockchain* chain, Formula** formulas, size_t count) {
    if (!chain || !formulas || count == 0) return false;
    
    // Проверка необходимости расширения
    if (chain->block_count >= chain->capacity) {
        size_t new_capacity = chain->capacity * 2;
        Block** new_blocks = (Block**)realloc(chain->blocks,
                                            sizeof(Block*) * new_capacity);
        if (!new_blocks) return false;
        
        chain->blocks = new_blocks;
        chain->capacity = new_capacity;
    }
    
    // Создание нового блока
    Block* block = (Block*)calloc(1, sizeof(Block));
    if (!block) return false;

    block->formulas = (Formula**)calloc(count, sizeof(Formula*));
    if (!block->formulas) {
        free(block);
        return false;
    }

    block->owned_formulas = (Formula*)calloc(count, sizeof(Formula));
    if (!block->owned_formulas) {
        free(block->formulas);
        free(block);
        return false;
    }

    block->formula_count = count;

    for (size_t i = 0; i < count; ++i) {
        Formula* src = formulas[i];
        Formula* dest = &block->owned_formulas[i];

        if (src) {
            if (formula_copy(dest, src) != 0) {
                for (size_t j = 0; j < i; ++j) {
                    formula_clear(&block->owned_formulas[j]);
                }
                formula_clear(dest);
                free(block->owned_formulas);
                free(block->formulas);
                free(block);
                return false;
            }
        }

        block->formulas[i] = dest;
    }

    double total_poe = 0.0;
    double total_mdl = 0.0;
    double total_score = 0.0;
    size_t sampled = 0;

    for (size_t i = 0; i < block->formula_count; ++i) {
        Formula* formula = block->formulas[i];
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

    // Установка времени создания
    block->timestamp = time(NULL);
    
    // Получение предыдущего хэша
    if (chain->block_count > 0) {
        strcpy(block->prev_hash, blockchain_get_last_hash(chain));
    } else {
        strcpy(block->prev_hash, GENESIS_PREV_HASH);
    }
    
    // Майнинг блока (поиск подходящего nonce)
    char hash[65];
    block->nonce = 0;
    do {
        block->nonce++;
        calculate_hash(block, hash);
    } while (strncmp(hash, DIFFICULTY_TARGET, strlen(DIFFICULTY_TARGET)) != 0);

    if (g_security_enabled) {
        if (blockchain_sign_block(block, hash) != 0) {
            log_error("blockchain: failed to sign block");
            for (size_t j = 0; j < block->formula_count; ++j) {
                formula_clear(&block->owned_formulas[j]);
            }
            free(block->owned_formulas);
            free(block->formulas);
            free(block);
            return false;
        }
    }

    // Добавление блока в цепочку
    chain->blocks[chain->block_count++] = block;

    // Добавление проверки размера блокчейна
    if (chain->block_count > MAX_BLOCKCHAIN_SIZE) {
        fprintf(stderr, "[ERROR] Blockchain size exceeded maximum limit. Consider pruning old blocks.\n");
        return false;
    }
    
    return true;
}

bool blockchain_verify(const Blockchain* chain) {
    if (!chain) return false;

    for (size_t i = 0; i < chain->block_count; ++i) {
        Block* block = chain->blocks[i];
        if (!block) {
            return false;
        }

        char current_hash[65];
        calculate_hash(block, current_hash);

        if (g_security_enabled && !blockchain_verify_signature(block, current_hash)) {
            return false;
        }

        if (strncmp(current_hash, DIFFICULTY_TARGET, strlen(DIFFICULTY_TARGET)) != 0) {
            return false;
        }

        if (i == 0) {
            if (strcmp(block->prev_hash, GENESIS_PREV_HASH) != 0) {
                return false;
            }
        } else {
            Block* prev_block = chain->blocks[i - 1];
            if (!prev_block) {
                return false;
            }

            char expected_prev_hash[65];
            calculate_hash(prev_block, expected_prev_hash);
            if (strcmp(block->prev_hash, expected_prev_hash) != 0) {
                return false;
            }
        }
    }

    return true;
}

const char* blockchain_get_last_hash(const Blockchain* chain) {
    if (!chain || chain->block_count == 0) {
        return GENESIS_PREV_HASH;
    }
    
    static char hash[65];
    calculate_hash(chain->blocks[chain->block_count - 1], hash);
    return hash;
}

void blockchain_destroy(Blockchain* chain) {
    if (!chain) return;

    for (size_t i = 0; i < chain->block_count; i++) {
        Block* block = chain->blocks[i];
        if (!block) {
            continue;
        }

        if (block->owned_formulas) {
            for (size_t j = 0; j < block->formula_count; ++j) {
                formula_clear(&block->owned_formulas[j]);
            }
            free(block->owned_formulas);
        }

        free(block->formulas);
        free(block);
    }

    free(chain->blocks);
    free(chain);
}
