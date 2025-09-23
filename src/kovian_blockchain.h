#ifndef KOVIAN_BLOCKCHAIN_H
#define KOVIAN_BLOCKCHAIN_H

#include "formula.h"
#include <time.h>
#include <stdint.h>

// Структура для подписи валидатора
typedef struct {
    char node_id[64];     // ID узла-валидатора
    char signature[256];  // Криптографическая подпись
} Validation;

// Структура блока
typedef struct Block {
    // Заголовок блока
    struct {
        uint32_t version;      // Версия протокола
        time_t timestamp;      // Время создания
        char prev_hash[65];    // Хеш предыдущего блока
        char merkle_root[65];  // Корень дерева Меркла
    } header;
    
    // Данные блока
    Formula* formulas;         // Массив формул
    size_t formula_count;      // Количество формул
    
    // Валидации
    Validation* validations;   // Массив подписей валидаторов
    size_t validation_count;   // Количество валидаций
    
    char hash[65];            // Хеш текущего блока
    struct Block* next;       // Указатель на следующий блок
} Block;

// Структура блокчейна
typedef struct {
    Block* genesis;       // Первый блок
    Block* latest;        // Последний блок
    size_t length;        // Длина цепочки
    double difficulty;    // Сложность для PoE
} KovianChain;

// Функции для работы с блокчейном
KovianChain* kovian_chain_create(void);
void kovian_chain_destroy(KovianChain* chain);
Block* kovian_chain_add_block(KovianChain* chain, Formula* formulas, size_t count);
int kovian_chain_validate(KovianChain* chain);
char* kovian_chain_serialize_block(const Block* block);
Block* kovian_chain_deserialize_block(const char* json);

// Функции для Proof of Effectiveness
double calculate_block_effectiveness(const Block* block);
int verify_block_effectiveness(const Block* block, double difficulty);
void adjust_chain_difficulty(KovianChain* chain);

#endif // KOVIAN_BLOCKCHAIN_H
