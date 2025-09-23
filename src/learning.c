#include "learning.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <json-c/json.h>
#include <time.h>
#include "network.h"

LearningSystem* learning_system_create(size_t initial_cell_count, double learning_rate) {
    LearningSystem* system = (LearningSystem*)malloc(sizeof(LearningSystem));
    if (!system) return NULL;
    
    system->cells = (DecimalCell**)calloc(initial_cell_count, sizeof(DecimalCell*));
    system->cell_count = 0;
    system->formulas = NULL;
    system->formula_count = 0;
    system->learning_rate = learning_rate;
    
    system->rules = rules_engine_create(0.5); // Порог по умолчанию
    
    if (!system->cells || !system->rules) {
        learning_system_destroy(system);
        return NULL;
    }
    
    return system;
}

bool learning_system_add_cell(LearningSystem* system, DecimalCell* cell) {
    if (!system || !cell) return false;
    
    DecimalCell** new_cells = (DecimalCell**)realloc(system->cells, 
                                                    (system->cell_count + 1) * sizeof(DecimalCell*));
    if (!new_cells) return false;
    
    system->cells = new_cells;
    system->cells[system->cell_count++] = cell;
    
    return true;
}

void learning_system_train(LearningSystem* system, double** training_data, size_t data_count) {
    if (!system || !training_data || data_count == 0) return;
    
    // Обучение на наборе данных
    for (size_t i = 0; i < data_count; i++) {
        // Обновление ячеек
        for (size_t j = 0; j < system->cell_count; j++) {
            decimal_cell_update(system->cells[j], training_data[i][j]);
        }
        
        // Оптимизация формул
        learning_system_optimize_formulas(system);
        
        // Генерация новых правил
        learning_system_generate_rules(system);
        
        // Добавление детализированного логирования
        fprintf(stdout, "[DEBUG] Training data point %zu\n", i);
        for (size_t j = 0; j < system->cell_count; j++) {
            fprintf(stdout, "[DEBUG] Cell %zu updated with value %.2f\n", j, training_data[i][j]);
        }
    }
}

void learning_system_generate_rules(LearningSystem* system) {
    if (!system) return;
    
    // Анализ паттернов в данных
    for (size_t i = 0; i < system->cell_count; i++) {
        for (size_t j = i + 1; j < system->cell_count; j++) {
            if (!system->cells[i]->active || !system->cells[j]->active) continue;
            
            // Пример простого правила: если значения коррелируют
            if (fabs(system->cells[i]->value - system->cells[j]->value) < 0.1) {
                char condition[100], action[100];
                snprintf(condition, sizeof(condition), "cells[%zu].value ≈ cells[%zu].value", i, j);
                snprintf(action, sizeof(action), "connect(%zu, %zu)", i, j);
                
                rules_engine_add_rule(system->rules, condition, action, 0.8);
            }
        }
    }
}

void learning_system_optimize_formulas(LearningSystem* system) {
    if (!system) return;
    
    // Оптимизация существующих формул
    for (size_t i = 0; i < system->formula_count; i++) {
        formula_optimize(system->formulas[i], system->cells, system->cell_count);
    }
    
    // Генерация новых формул при необходимости
    if (system->formula_count < system->cell_count) {
        Formula* new_formula = formula_generate_from_cells(system->cells, system->cell_count);
        if (new_formula) {
            Formula** new_formulas = (Formula**)realloc(system->formulas, 
                                                      (system->formula_count + 1) * sizeof(Formula*));
            if (new_formulas) {
                system->formulas = new_formulas;
                system->formulas[system->formula_count++] = new_formula;
            } else {
                formula_destroy(new_formula);
            }
        }
    }
}

bool learning_system_federated_update(LearningSystem* system,
                                     const char* remote_host,
                                     int remote_port) {
    if (!system || !remote_host || remote_port <= 0) return false;
    
    // Подготовка данных для обмена
    struct json_object* root = json_object_new_object();
    
    // Добавление формул
    struct json_object* formulas_array = json_object_new_array();
    for (size_t i = 0; i < system->formula_count; i++) {
        struct json_object* formula_obj = json_object_new_object();
        json_object_object_add(formula_obj, "type", 
                             json_object_new_int(system->formulas[i]->type));
        json_object_object_add(formula_obj, "effectiveness", 
                             json_object_new_double(system->formulas[i]->effectiveness));
        json_object_array_add(formulas_array, formula_obj);
    }
    json_object_object_add(root, "formulas", formulas_array);
    
    struct json_object* envelope = json_object_new_object();
    json_object_object_add(envelope, "type", json_object_new_string("federated_update"));
    json_object_object_add(envelope, "payload", root);

    const char* payload = json_object_to_json_string(envelope);
    bool sent = network_send_data(remote_host, remote_port, payload);
    if (sent) {
        fprintf(stdout, "[NETWORK] Federated update delivered to %s:%d\n", remote_host, remote_port);
    } else {
        fprintf(stderr, "[NETWORK] Failed to deliver federated update to %s:%d\n", remote_host, remote_port);
    }

    json_object_put(envelope);
    return sent;
}

bool learning_system_export_knowledge(LearningSystem* system, const char* filename) {
    if (!system || !filename) return false;
    
    FILE* file = fopen(filename, "w");
    if (!file) return false;
    
    // Экспорт формул
    fprintf(file, "Formulas:\n");
    for (size_t i = 0; i < system->formula_count; i++) {
        fprintf(file, "Formula %zu: Type=%d, Effectiveness=%.4f\n", 
                i, system->formulas[i]->type, system->formulas[i]->effectiveness);
        
        for (size_t j = 0; j < system->formulas[i]->coeff_count; j++) {
            fprintf(file, "  Coeff[%zu]=%.6f\n", j, system->formulas[i]->coefficients[j]);
        }
    }
    
    // Экспорт правил
    fprintf(file, "\nRules:\n");
    for (size_t i = 0; i < system->rules->count; i++) {
        fprintf(file, "Rule %zu: Weight=%.4f, Enabled=%d\n", 
                i, system->rules->rules[i]->weight, system->rules->rules[i]->enabled);
        fprintf(file, "  Condition: %s\n", system->rules->rules[i]->condition);
        fprintf(file, "  Action: %s\n", system->rules->rules[i]->action);
    }
    
    fclose(file);
    return true;
}

bool learning_system_import_knowledge(LearningSystem* system, const char* filename) {
    if (!system || !filename) return false;
    
    FILE* file = fopen(filename, "r");
    if (!file) return false;
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Парсинг и импорт данных
        // TODO: Реализовать парсинг формата экспорта
    }
    
    fclose(file);
    return true;
}

void learning_system_destroy(LearningSystem* system) {
    if (!system) return;
    
    // Освобождение ячеек
    for (size_t i = 0; i < system->cell_count; i++) {
        decimal_cell_destroy(system->cells[i]);
    }
    free(system->cells);
    
    // Освобождение формул
    for (size_t i = 0; i < system->formula_count; i++) {
        formula_destroy(system->formulas[i]);
    }
    free(system->formulas);
    
    // Освобождение движка правил
    rules_engine_destroy(system->rules);
    
    free(system);
}

// Расширенное логирование времени выполнения операций
void log_execution_time(const char* operation, clock_t start_time) {
    clock_t end_time = clock();
    double elapsed_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    fprintf(stdout, "[LOG] %s took %.2f seconds\n", operation, elapsed_time);
}

// Пример использования
void example_usage() {
    clock_t start_time = clock();
    // ...код операции...
    log_execution_time("Training step", start_time);
}
