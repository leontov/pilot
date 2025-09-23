#include "blockchain.h"
#include "formula.h"
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <stdio.h>

#define INITIAL_CAPACITY 16
#define DIFFICULTY_TARGET "000" // Первые три символа должны быть нулями
#define MAX_BLOCKCHAIN_SIZE 1000 // Максимальный размер блокчейна

static const char GENESIS_PREV_HASH[] =
    "0000000000000000000000000000000000000000000000000000000000000000";

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
                             strnlen(formula->content, sizeof(formula->content)));
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

static Formula* blockchain_clone_formula(const Formula* src) {
    if (!src) {
        return NULL;
    }

    Formula* clone = (Formula*)calloc(1, sizeof(Formula));
    if (!clone) {
        return NULL;
    }

    if (formula_copy(clone, src) != 0) {
        free(clone);
        return NULL;
    }

    return clone;
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

            }
            free(block->owned_formulas);
        }

        free(block->formulas);
        free(block);
    }
    
    free(chain->blocks);
    free(chain);
}
