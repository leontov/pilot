#include "kovian_blockchain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <json-c/json.h>

// Создание нового блокчейна
KovianChain* kovian_chain_create(void) {
    KovianChain* chain = malloc(sizeof(KovianChain));
    if (!chain) return NULL;
    
    // Инициализация цепочки
    chain->genesis = NULL;
    chain->latest = NULL;
    chain->length = 0;
    chain->difficulty = 0.7; // Начальная сложность
    
    return chain;
}

// Уничтожение блокчейна
void kovian_chain_destroy(KovianChain* chain) {
    if (!chain) return;
    
    // Освобождаем все блоки
    Block* current = chain->genesis;
    while (current) {
        Block* next = current->next;
        if (current->formulas) {
            for (size_t i = 0; i < current->formula_count; i++) {
                formula_clear(&current->formulas[i]);
            }
            free(current->formulas);
        }
        free(current->validations);
        free(current);
        current = next;
    }
    
    free(chain);
}

// Вычисление хеша блока
static void calculate_block_hash(Block* block) {
    EVP_MD_CTX *mdctx;
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;

    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
    
    // Хешируем заголовок
    EVP_DigestUpdate(mdctx, &block->header, sizeof(block->header));
    
    // Хешируем формулы
    for (size_t i = 0; i < block->formula_count; i++) {
        Formula* formula = &block->formulas[i];

        EVP_DigestUpdate(mdctx, formula->id,
                         strnlen(formula->id, sizeof(formula->id)));
        EVP_DigestUpdate(mdctx, &formula->effectiveness, sizeof(formula->effectiveness));
        EVP_DigestUpdate(mdctx, &formula->created_at, sizeof(formula->created_at));
        EVP_DigestUpdate(mdctx, &formula->tests_passed, sizeof(formula->tests_passed));
        EVP_DigestUpdate(mdctx, &formula->confirmations, sizeof(formula->confirmations));
        EVP_DigestUpdate(mdctx, &formula->representation, sizeof(formula->representation));

        if (formula->representation == FORMULA_REPRESENTATION_ANALYTIC) {
            EVP_DigestUpdate(mdctx, &formula->type, sizeof(formula->type));
            if (formula->coeff_count > 0 && formula->coefficients) {
                EVP_DigestUpdate(mdctx, formula->coefficients,
                                 sizeof(double) * formula->coeff_count);
            }
            if (formula->expression) {
                EVP_DigestUpdate(mdctx, formula->expression,
                                 strlen(formula->expression));
            }
        } else {
            EVP_DigestUpdate(mdctx, formula->content,
                             strnlen(formula->content, sizeof(formula->content)));
        }
    }
    
    // Хешируем валидации
    for (size_t i = 0; i < block->validation_count; i++) {
        EVP_DigestUpdate(mdctx, &block->validations[i], sizeof(Validation));
    }
    
    EVP_DigestFinal_ex(mdctx, hash, &hash_len);
    EVP_MD_CTX_free(mdctx);
    
    // Конвертируем в hex строку
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        snprintf(&block->hash[i*2], 3, "%02x", hash[i]);
    }
    block->hash[64] = '\0';
}

// Добавление нового блока
Block* kovian_chain_add_block(KovianChain* chain, Formula* formulas, size_t count) {
    if (!chain || !formulas || count == 0) return NULL;
    
    Block* block = malloc(sizeof(Block));
    if (!block) return NULL;
    
    // Инициализация блока
    block->header.version = 1;
    block->header.timestamp = time(NULL);
    if (chain->latest) {
        strcpy(block->header.prev_hash, chain->latest->hash);
    } else {
        memset(block->header.prev_hash, '0', 64);
        block->header.prev_hash[64] = '\0';
    }
    
    // Копируем формулы
    block->formulas = calloc(count, sizeof(Formula));
    if (!block->formulas) {
        free(block);
        return NULL;
    }
    block->formula_count = count;

    for (size_t i = 0; i < count; i++) {
        if (formula_copy(&block->formulas[i], &formulas[i]) != 0) {
            for (size_t j = 0; j < i; j++) {
                formula_clear(&block->formulas[j]);
            }
            free(block->formulas);
            free(block);
            return NULL;
        }
    }
    
    // Инициализируем валидации
    block->validations = NULL;
    block->validation_count = 0;
    
    // Вычисляем хеш
    calculate_block_hash(block);
    
    // Добавляем в цепочку
    block->next = NULL;
    if (!chain->genesis) {
        chain->genesis = block;
    } else {
        chain->latest->next = block;
    }
    chain->latest = block;
    chain->length++;
    
    return block;
}

// Проверка целостности цепочки
int kovian_chain_validate(KovianChain* chain) {
    if (!chain || !chain->genesis) return 0;
    
    Block* current = chain->genesis;
    char prev_hash[65];
    memset(prev_hash, '0', 64);
    prev_hash[64] = '\0';
    
    while (current) {
        // Проверяем связь с предыдущим блоком
        if (strcmp(current->header.prev_hash, prev_hash) != 0) {
            return 0;
        }
        
        // Проверяем PoE
        if (!verify_block_effectiveness(current, chain->difficulty)) {
            return 0;
        }
        
        // Проверяем хеш блока
        char temp_hash[65];
        strcpy(temp_hash, current->hash);
        calculate_block_hash(current);
        if (strcmp(temp_hash, current->hash) != 0) {
            return 0;
        }
        
        strcpy(prev_hash, current->hash);
        current = current->next;
    }
    
    return 1;
}

// Расчет эффективности блока (PoE)
double calculate_block_effectiveness(const Block* block) {
    if (!block || block->formula_count == 0) return 0.0;
    
    double total_effectiveness = 0.0;
    for (size_t i = 0; i < block->formula_count; i++) {
        total_effectiveness += block->formulas[i].effectiveness;
    }
    
    return total_effectiveness / block->formula_count;
}

// Проверка соответствия блока требуемой сложности
int verify_block_effectiveness(const Block* block, double difficulty) {
    return calculate_block_effectiveness(block) >= difficulty;
}

// Корректировка сложности сети
void adjust_chain_difficulty(KovianChain* chain) {
    if (!chain || chain->length < 100) return;
    
    // Анализируем последние 100 блоков, проходя цепочку от genesis
    double window[100] = {0};
    size_t count = 0;
    size_t index = 0;
    double window_sum = 0.0;

    Block* current = chain->genesis;
    while (current) {
        double effectiveness = calculate_block_effectiveness(current);
        if (count < 100) {
            window[count++] = effectiveness;
            window_sum += effectiveness;
        } else {
            window_sum -= window[index];
            window[index] = effectiveness;
            window_sum += effectiveness;
            index = (index + 1) % 100;
        }

        current = current->next;
    }

    if (count < 100) {
        return;
    }

    double avg_effectiveness = window_sum / 100.0;
    
    // Корректируем сложность
    if (avg_effectiveness > chain->difficulty * 1.1) {
        chain->difficulty *= 1.1; // Увеличиваем сложность
    } else if (avg_effectiveness < chain->difficulty * 0.9) {
        chain->difficulty *= 0.9; // Уменьшаем сложность
    }
    
    // Ограничиваем диапазон сложности
    if (chain->difficulty < 0.1) chain->difficulty = 0.1;
    if (chain->difficulty > 0.9) chain->difficulty = 0.9;
}
