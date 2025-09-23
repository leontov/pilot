#include "formula_advanced.h"
#include "formula.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Formula* formula_create(FormulaType type, size_t coeff_count) {
    Formula* formula = (Formula*)calloc(1, sizeof(Formula));
    if (!formula) return NULL;

    formula->coefficients = (double*)calloc(coeff_count, sizeof(double));
    if (!formula->coefficients) {
        free(formula);
        return NULL;
    }

    formula->type = type;
    formula->coeff_count = coeff_count;
    formula->effectiveness = 0.0;
    formula->expression = NULL;
    formula->representation = FORMULA_REPRESENTATION_ANALYTIC;
    formula->created_at = time(NULL);
    formula->tests_passed = 0;
    formula->confirmations = 0;
    formula->content[0] = '\0';

    return formula;
}

Formula* formula_generate_from_cells(DecimalCell** cells, size_t cell_count) {
    if (!cells || cell_count == 0) return NULL;
    
    // Определяем тип формулы на основе анализа данных
    FormulaType type = FORMULA_LINEAR;
    size_t coeff_count = 2; // Для линейной формулы
    
    double sum = 0.0, sum_sq = 0.0;
    for (size_t i = 0; i < cell_count; i++) {
        if (cells[i]->active) {
            sum += cells[i]->value;
            sum_sq += cells[i]->value * cells[i]->value;
        }
    }
    
    // Если квадратичное отклонение большое, используем полином
    if (sum_sq / cell_count - (sum / cell_count) * (sum / cell_count) > 1.0) {
        type = FORMULA_POLYNOMIAL;
        coeff_count = 3;
    }
    
    Formula* formula = formula_create(type, coeff_count);
    if (!formula) return NULL;
    
    // Инициализация коэффициентов
    for (size_t i = 0; i < coeff_count; i++) {
        formula->coefficients[i] = (double)rand() / RAND_MAX * 2.0 - 1.0;
    }
    
    return formula;
}

bool formula_optimize(Formula* formula, DecimalCell** cells, size_t cell_count) {
    if (!formula || !cells) return false;
    
    const double learning_rate = 0.01;
    const int iterations = 100;
    
    for (int iter = 0; iter < iterations; iter++) {
        double error_sum = 0.0;
        
        for (size_t i = 0; i < cell_count; i++) {
            if (!cells[i]->active) continue;
            
            double predicted = formula_evaluate(formula, cells[i]->value);
            double error = cells[i]->value - predicted;
            error_sum += error * error;
            
            // Градиентный спуск
            for (size_t j = 0; j < formula->coeff_count; j++) {
                formula->coefficients[j] += learning_rate * error;
            }
        }
        
        // Обновление эффективности
        formula->effectiveness = 1.0 / (1.0 + sqrt(error_sum / cell_count));
    }
    
    return true;
}

double formula_evaluate(Formula* formula, double x) {
    if (!formula) return 0.0;
    
    double result = 0.0;
    
    switch (formula->type) {
        case FORMULA_LINEAR:
            result = formula->coefficients[0] * x + formula->coefficients[1];
            break;
            
        case FORMULA_POLYNOMIAL:
            result = formula->coefficients[0] * x * x + 
                    formula->coefficients[1] * x + 
                    formula->coefficients[2];
            break;
            
        case FORMULA_EXPONENTIAL:
            result = formula->coefficients[0] * exp(formula->coefficients[1] * x);
            break;
            
        case FORMULA_TRIGONOMETRIC:
            result = formula->coefficients[0] * sin(formula->coefficients[1] * x);
            break;
            
        case FORMULA_COMPOSITE:
            // Сложная формула с комбинацией разных типов
            result = formula->coefficients[0] * sin(formula->coefficients[1] * x) +
                    formula->coefficients[2] * x * x;
            break;
    }
    
    return result;
}

double formula_calculate_effectiveness(Formula* formula, DecimalCell** cells, size_t cell_count) {
    if (!formula || !cells) return 0.0;
    
    double error_sum = 0.0;
    size_t active_count = 0;
    
    for (size_t i = 0; i < cell_count; i++) {
        if (!cells[i]->active) continue;
        
        double predicted = formula_evaluate(formula, cells[i]->value);
        double error = fabs(cells[i]->value - predicted);
        error_sum += error;
        active_count++;
    }
    
    if (active_count == 0) return 0.0;
    
    formula->effectiveness = 1.0 / (1.0 + error_sum / active_count);
    return formula->effectiveness;
}

Formula* formula_mutate(Formula* formula, double mutation_rate) {
    if (!formula) return NULL;
    
    Formula* mutated = formula_create(formula->type, formula->coeff_count);
    if (!mutated) return NULL;

    mutated->effectiveness = formula->effectiveness;
    mutated->created_at = formula->created_at;
    mutated->tests_passed = formula->tests_passed;
    mutated->confirmations = formula->confirmations;
    if (formula->expression) {
        mutated->expression = strdup(formula->expression);
        if (!mutated->expression) {
            formula_destroy(mutated);
            return NULL;
        }
    }

    // Копируем коэффициенты с возможными мутациями
    for (size_t i = 0; i < formula->coeff_count; i++) {
        if ((double)rand() / RAND_MAX < mutation_rate) {
            mutated->coefficients[i] = formula->coefficients[i] * 
                (1.0 + ((double)rand() / RAND_MAX - 0.5) * 0.2);
        } else {
            mutated->coefficients[i] = formula->coefficients[i];
        }
    }
    
    return mutated;
}

Formula* formula_crossover(Formula* formula1, Formula* formula2) {
    if (!formula1 || !formula2 || formula1->type != formula2->type) return NULL;
    
    Formula* child = formula_create(formula1->type, formula1->coeff_count);
    if (!child) return NULL;

    child->effectiveness = (formula1->effectiveness + formula2->effectiveness) / 2.0;
    child->created_at = time(NULL);
    child->tests_passed = 0;
    child->confirmations = 0;
    if (formula1->expression) {
        child->expression = strdup(formula1->expression);
        if (!child->expression) {
            formula_destroy(child);
            return NULL;
        }
    }

    // Одноточечное скрещивание
    size_t crossover_point = rand() % formula1->coeff_count;

    for (size_t i = 0; i < formula1->coeff_count; i++) {
        if (i < crossover_point) {
            child->coefficients[i] = formula1->coefficients[i];
        } else {
            child->coefficients[i] = formula2->coefficients[i];
        }
    }
    
    return child;
}

void formula_destroy(Formula* formula) {
    if (!formula) return;

    formula_clear(formula);
    free(formula);
}
