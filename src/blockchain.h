#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include <stdbool.h>
#include <time.h>
#include "formula_advanced.h"

typedef struct {
    Formula** formulas;
    Formula* owned_formulas;
    size_t formula_count;
    char prev_hash[65];
    time_t timestamp;
    uint32_t nonce;
    double poe_sum;
    double poe_average;
    double mdl_sum;
    double mdl_average;
    double score_sum;
    double score_average;
} Block;

typedef struct {
    Block** blocks;
    size_t block_count;
    size_t capacity;
} Blockchain;

// Создание новой цепочки блоков
Blockchain* blockchain_create(void);

// Добавление нового блока с формулами
bool blockchain_add_block(Blockchain* chain, Formula** formulas, size_t count);

// Проверка целостности цепочки
bool blockchain_verify(const Blockchain* chain);

// Получение хэша последнего блока
const char* blockchain_get_last_hash(const Blockchain* chain);

double blockchain_score_formula(const Formula* formula, double* poe_out, double* mdl_out);

// Освобождение ресурсов
void blockchain_destroy(Blockchain* chain);

#endif // BLOCKCHAIN_H
