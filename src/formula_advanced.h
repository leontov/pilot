#ifndef FORMULA_ADVANCED_H
#define FORMULA_ADVANCED_H

#include <stdbool.h>
#include <stddef.h>
#include "decimal_cell.h"
#include "formula_core.h"

// Создание новой формулы
Formula* formula_create(FormulaType type, size_t coeff_count);

// Генерация формулы на основе данных ячеек
Formula* formula_generate_from_cells(DecimalCell** cells, size_t cell_count);

// Оптимизация коэффициентов
bool formula_optimize(Formula* formula, DecimalCell** cells, size_t cell_count);

// Вычисление значения
double formula_evaluate(Formula* formula, double x);

// Расчет эффективности
double formula_calculate_effectiveness(Formula* formula, DecimalCell** cells, size_t cell_count);

// Мутация формулы
Formula* formula_mutate(Formula* formula, double mutation_rate);

// Скрещивание формул
Formula* formula_crossover(Formula* formula1, Formula* formula2);

// Освобождение ресурсов
void formula_destroy(Formula* formula);

#endif // FORMULA_ADVANCED_H
