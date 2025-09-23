#ifndef ABSTRACT_MIND_H
#define ABSTRACT_MIND_H
#include <time.h>

// Структура для формулы или выражения
typedef struct {
    char expression[1024];  // Увеличиваем буфер для более сложных выражений
    double effectiveness;
    time_t timestamp;
} Formula;

// Генерация начальной формулы
Formula generate_formula(void);

// Эволюция формулы на основе входных данных
Formula evolve_formula(Formula initial);

#endif // ABSTRACT_MIND_H
