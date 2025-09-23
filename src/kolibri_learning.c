#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "kolibri_learning.h"

#define MIN_CONFIDENCE 0.1
#define MAX_COMBINATIONS 1000
#define COMBINATION_THRESHOLD 0.7

int init_learning(learning_context_t* ctx, rules_t* rules) {
    ctx->stats = calloc(MAX_RULES, sizeof(rule_stats_t));
    ctx->combinations = calloc(MAX_COMBINATIONS, sizeof(rule_combination_t));
    
    if (!ctx->stats || !ctx->combinations) {
        free(ctx->stats);
        free(ctx->combinations);
        return -1;
    }
    
    ctx->n_combinations = 0;
    ctx->learning_rate = 0.1;
    ctx->exploration_rate = 0.2;
    
    return 0;
}

void update_rule_stats(learning_context_t* ctx, int rule_idx, bool success, double response_time) {
    if (rule_idx < 0 || rule_idx >= MAX_RULES) return;
    
    rule_stats_t* stats = &ctx->stats[rule_idx];
    stats->total_uses++;
    if (success) {
        stats->successful_uses++;
    }
    
    // Экспоненциальное скользящее среднее для времени отклика
    double alpha = 0.1;
    stats->avg_response_time = alpha * response_time + (1 - alpha) * stats->avg_response_time;
    
    // Обновление уверенности
    stats->confidence = (double)stats->successful_uses / stats->total_uses;
}

int discover_combinations(learning_context_t* ctx, rules_t* rules) {
    if (ctx->n_combinations >= MAX_COMBINATIONS) return -1;
    
    // Ищем правила, которые часто используются вместе и дают хороший результат
    for (int i = 0; i < rules->count; i++) {
        for (int j = i + 1; j < rules->count; j++) {
            if (ctx->stats[i].confidence > COMBINATION_THRESHOLD &&
                ctx->stats[j].confidence > COMBINATION_THRESHOLD) {
                
                // Проверяем, нет ли уже такой комбинации
                bool exists = false;
                for (int k = 0; k < ctx->n_combinations; k++) {
                    if ((ctx->combinations[k].rule1_idx == i && ctx->combinations[k].rule2_idx == j) ||
                        (ctx->combinations[k].rule1_idx == j && ctx->combinations[k].rule2_idx == i)) {
                        exists = true;
                        break;
                    }
                }
                
                if (!exists) {
                    ctx->combinations[ctx->n_combinations].rule1_idx = i;
                    ctx->combinations[ctx->n_combinations].rule2_idx = j;
                    ctx->combinations[ctx->n_combinations].joint_fitness = 
                        (ctx->stats[i].confidence + ctx->stats[j].confidence) / 2.0;
                    ctx->combinations[ctx->n_combinations].co_occurrences = 1;
                    ctx->n_combinations++;
                }
            }
        }
    }
    
    return 0;
}

int create_composite_rule(learning_context_t* ctx, rules_t* rules,
                         int rule1_idx, int rule2_idx) {
    if (rule1_idx < 0 || rule1_idx >= rules->count ||
        rule2_idx < 0 || rule2_idx >= rules->count) {
        return -1;
    }
    
    // Создаем новое правило, объединяющее два существующих
    char new_pattern[MAX_PATTERN_LEN];
    char new_action[MAX_ACTION_LEN];
    
    // Объединяем паттерны с разделителем
    snprintf(new_pattern, sizeof(new_pattern), "%s_%s",
             rules->patterns[rule1_idx], rules->patterns[rule2_idx]);
    
    // Объединяем действия
    snprintf(new_action, sizeof(new_action), "%s_%s",
             rules->actions[rule1_idx], rules->actions[rule2_idx]);
    
    // Используем максимальный уровень из родительских правил + 1
    int new_tier = fmax(rules->tiers[rule1_idx], rules->tiers[rule2_idx]) + 1;
    
    // Начальная fitness - среднее между родительскими
    double new_fitness = (rules->fitness[rule1_idx] + rules->fitness[rule2_idx]) / 2.0;
    
    return add_rule(rules, new_pattern, new_action, new_tier, new_fitness);
}

int prune_ineffective_rules(learning_context_t* ctx, rules_t* rules) {
    int removed = 0;
    
    for (int i = 0; i < rules->count; i++) {
        if (ctx->stats[i].total_uses > 10 && // Минимальное количество использований
            ctx->stats[i].confidence < MIN_CONFIDENCE) {
            
            // Сдвигаем все правила на одну позицию влево
            for (int j = i; j < rules->count - 1; j++) {
                memcpy(rules->patterns[j], rules->patterns[j+1], MAX_PATTERN_LEN);
                memcpy(rules->actions[j], rules->actions[j+1], MAX_ACTION_LEN);
                rules->tiers[j] = rules->tiers[j+1];
                rules->fitness[j] = rules->fitness[j+1];
                ctx->stats[j] = ctx->stats[j+1];
            }
            
            rules->count--;
            removed++;
            i--; // Проверим текущую позицию снова
        }
    }
    
    return removed;
}

void adapt_learning_params(learning_context_t* ctx, double success_rate) {
    // Адаптация скорости обучения
    if (success_rate > 0.8) {
        ctx->learning_rate *= 0.95; // Уменьшаем, если успешность высокая
    } else if (success_rate < 0.5) {
        ctx->learning_rate *= 1.05; // Увеличиваем, если успешность низкая
    }
    
    // Ограничиваем значения
    ctx->learning_rate = fmax(0.01, fmin(0.5, ctx->learning_rate));
    
    // Адаптация уровня исследования
    if (success_rate > 0.9) {
        ctx->exploration_rate *= 1.1; // Больше исследуем при высокой успешности
    } else if (success_rate < 0.3) {
        ctx->exploration_rate *= 0.9; // Меньше исследуем при низкой успешности
    }
    
    ctx->exploration_rate = fmax(0.05, fmin(0.3, ctx->exploration_rate));
}
