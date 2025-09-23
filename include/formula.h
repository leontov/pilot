#ifndef FORMULA_H
#define FORMULA_H

#include <stddef.h>
#include <time.h>

// Типы формул
extern const int FORMULA_TYPE_SIMPLE;
extern const int FORMULA_TYPE_POLYNOMIAL;
extern const int FORMULA_TYPE_COMPOSITE;
extern const int FORMULA_TYPE_PERIODIC;

// Структура формулы
typedef struct {
    char id[37];                    // UUID формулы
    char content[1024];             // Текст формулы
    double effectiveness;           // Оценка эффективности
    time_t created_at;             // Время создания
    int tests_passed;              // Количество успешных тестов
    int confirmations;             // Количество подтверждений от других нод
} Formula;

// Коллекция формул
typedef struct {
    Formula* formulas;             // Массив формул
    size_t count;                  // Текущее количество
    size_t capacity;               // Максимальная емкость
} FormulaCollection;

// Функции
FormulaCollection* formula_collection_create(size_t initial_capacity);
void formula_collection_destroy(FormulaCollection* collection);
int formula_collection_add(FormulaCollection* collection, const Formula* formula);
double evaluate_formula(const char* formula, double x);
double evaluate_effectiveness(const Formula* formula);
int validate_formula(const Formula* formula);
int get_formula_type(const char* content);

#endif // FORMULA_H
