#ifndef FORMULA_H
#define FORMULA_H

#include <time.h>
#include <stdint.h>

// Структура для представления формулы
typedef struct {
    char id[64];           // Уникальный идентификатор формулы
    char content[1024];    // Содержимое формулы в закодированном виде
    double effectiveness;  // Оценка эффективности (0.0 - 1.0)
    time_t created_at;    // Время создания
    uint32_t tests_passed; // Количество успешных тестов
    uint32_t confirmations; // Количество подтверждений от других узлов
} Formula;

// Структура для хранения коллекции формул
typedef struct {
    Formula* formulas;     // Массив формул
    size_t count;         // Текущее количество формул
    size_t capacity;      // Максимальная емкость
} FormulaCollection;

// Функции для работы с формулами
FormulaCollection* formula_collection_create(size_t initial_capacity);
void formula_collection_destroy(FormulaCollection* collection);
int formula_collection_add(FormulaCollection* collection, const Formula* formula);
Formula* formula_collection_find(FormulaCollection* collection, const char* id);
void formula_collection_remove(FormulaCollection* collection, const char* id);

// Функции генерации и валидации
Formula* generate_random_formula(int complexity_level);
int validate_formula(const Formula* formula);
double evaluate_effectiveness(const Formula* formula);
char* serialize_formula(const Formula* formula);
Formula* deserialize_formula(const char* json);

#endif // FORMULA_H
