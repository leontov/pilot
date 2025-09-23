#include "rules_engine.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16

RulesEngine* rules_engine_create(double threshold) {
    RulesEngine* engine = (RulesEngine*)malloc(sizeof(RulesEngine));
    if (!engine) return NULL;
    
    engine->rules = (Rule**)malloc(sizeof(Rule*) * INITIAL_CAPACITY);
    if (!engine->rules) {
        free(engine);
        return NULL;
    }
    
    engine->count = 0;
    engine->capacity = INITIAL_CAPACITY;
    engine->threshold = threshold;
    
    return engine;
}

bool rules_engine_add_rule(RulesEngine* engine, const char* condition, 
                          const char* action, double weight) {
    if (!engine || !condition || !action) return false;
    
    // Проверка необходимости расширения массива
    if (engine->count >= engine->capacity) {
        size_t new_capacity = engine->capacity * 2;
        Rule** new_rules = (Rule**)realloc(engine->rules, 
                                          sizeof(Rule*) * new_capacity);
        if (!new_rules) return false;
        
        engine->rules = new_rules;
        engine->capacity = new_capacity;
    }
    
    // Создание нового правила
    Rule* rule = (Rule*)malloc(sizeof(Rule));
    if (!rule) return false;
    
    rule->condition = strdup(condition);
    rule->action = strdup(action);
    rule->weight = weight;
    rule->enabled = true;
    
    if (!rule->condition || !rule->action) {
        free(rule->condition);
        free(rule->action);
        free(rule);
        return false;
    }
    
    engine->rules[engine->count++] = rule;
    return true;
}

void rules_engine_process(RulesEngine* engine, DecimalCell** cells, size_t cell_count) {
    if (!engine || !cells) return;
    
    for (size_t i = 0; i < engine->count; i++) {
        Rule* rule = engine->rules[i];
        if (!rule->enabled || rule->weight < engine->threshold) continue;
        
        // Здесь должна быть логика проверки условия и выполнения действия
        // Это упрощенная реализация
        for (size_t j = 0; j < cell_count; j++) {
            if (cells[j]->active) {
                // Пример простого действия - увеличение значения ячейки
                decimal_cell_update(cells[j], cells[j]->value * rule->weight);
            }
        }
    }
}

bool rules_engine_adjust_weight(RulesEngine* engine, size_t rule_index, double delta) {
    if (!engine || rule_index >= engine->count) return false;
    
    engine->rules[rule_index]->weight += delta;
    return true;
}

void rules_engine_set_rule_enabled(RulesEngine* engine, size_t rule_index, bool enabled) {
    if (engine && rule_index < engine->count) {
        engine->rules[rule_index]->enabled = enabled;
    }
}

void rules_engine_destroy(RulesEngine* engine) {
    if (!engine) return;
    
    for (size_t i = 0; i < engine->count; i++) {
        if (engine->rules[i]) {
            free(engine->rules[i]->condition);
            free(engine->rules[i]->action);
            free(engine->rules[i]);
        }
    }
    
    free(engine->rules);
    free(engine);
}
