#ifndef KOLIBRI_LEARNING_H
#define KOLIBRI_LEARNING_H

#include "kolibri_rules.h"

// Структура для хранения статистики применения правила
typedef struct {
    uint64_t total_uses;        // Общее количество применений
    uint64_t successful_uses;   // Успешные применения
    double avg_response_time;   // Среднее время отклика
    double confidence;          // Уверенность в правиле [0..1]
} rule_stats_t;

// Структура для отслеживания эффективности комбинаций правил
typedef struct {
    int rule1_idx;             // Индекс первого правила
    int rule2_idx;             // Индекс второго правила
    double joint_fitness;      // Совместная эффективность
    uint64_t co_occurrences;   // Количество совместных появлений
} rule_combination_t;

// Структура контекста обучения
typedef struct {
    rule_stats_t* stats;                   // Статистика по каждому правилу
    rule_combination_t* combinations;       // Комбинации правил
    int n_combinations;                     // Количество отслеживаемых комбинаций
    double learning_rate;                   // Скорость обучения [0..1]
    double exploration_rate;                // Вероятность исследования новых правил
} learning_context_t;

// Инициализация контекста обучения
int init_learning(learning_context_t* ctx, rules_t* rules);

// Обновление статистики после применения правила
void update_rule_stats(learning_context_t* ctx, int rule_idx, bool success, double response_time);

// Поиск потенциально полезных комбинаций правил
int discover_combinations(learning_context_t* ctx, rules_t* rules);

// Создание нового правила на основе успешной комбинации
int create_composite_rule(learning_context_t* ctx, rules_t* rules, 
                         int rule1_idx, int rule2_idx);

// Очистка неэффективных правил
int prune_ineffective_rules(learning_context_t* ctx, rules_t* rules);

// Адаптация параметров обучения
void adapt_learning_params(learning_context_t* ctx, double success_rate);

#endif
