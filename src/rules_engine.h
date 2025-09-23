#ifndef RULES_ENGINE_H
#define RULES_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include "decimal_cell.h"

typedef struct {
    char* condition;        // Строка с условием
    char* action;          // Строка с действием
    double weight;         // Вес правила
    bool enabled;          // Флаг активности
} Rule;

typedef struct {
    Rule** rules;          // Массив правил
    size_t count;          // Количество правил
    size_t capacity;       // Емкость массива
    double threshold;      // Порог срабатывания
} RulesEngine;

// Создание движка правил
RulesEngine* rules_engine_create(double threshold);

// Добавление нового правила
bool rules_engine_add_rule(RulesEngine* engine, const char* condition, 
                          const char* action, double weight);

// Выполнение правил для набора ячеек
void rules_engine_process(RulesEngine* engine, DecimalCell** cells, size_t cell_count);

// Изменение веса правила
bool rules_engine_adjust_weight(RulesEngine* engine, size_t rule_index, double delta);

// Включение/выключение правила
void rules_engine_set_rule_enabled(RulesEngine* engine, size_t rule_index, bool enabled);

// Освобождение ресурсов
void rules_engine_destroy(RulesEngine* engine);

#endif // RULES_ENGINE_H
