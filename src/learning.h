#ifndef LEARNING_H
#define LEARNING_H

#include <stddef.h>
#include "decimal_cell.h"
#include "rules_engine.h"
#include "formula_advanced.h"

typedef struct {
    DecimalCell** cells;
    size_t cell_count;
    RulesEngine* rules;
    Formula** formulas;
    size_t formula_count;
    double learning_rate;
} LearningSystem;

// Создание системы обучения
LearningSystem* learning_system_create(size_t initial_cell_count, double learning_rate);

// Добавление новой ячейки в систему
bool learning_system_add_cell(LearningSystem* system, DecimalCell* cell);

// Обучение на наборе данных
void learning_system_train(LearningSystem* system, double** training_data, size_t data_count);

// Генерация новых правил на основе наблюдений
void learning_system_generate_rules(LearningSystem* system);

// Оптимизация формул
void learning_system_optimize_formulas(LearningSystem* system);

// Федеративное обучение - обмен опытом между узлами
bool learning_system_federated_update(LearningSystem* system,
                                    const char* remote_host,
                                    int remote_port);

// Экспорт накопленных знаний
bool learning_system_export_knowledge(LearningSystem* system, const char* filename);

// Импорт знаний
bool learning_system_import_knowledge(LearningSystem* system, const char* filename);

// Освобождение ресурсов
void learning_system_destroy(LearningSystem* system);

#endif // LEARNING_H
