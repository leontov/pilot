#ifndef KOLIBRI_FORMULA_CORE_H
#define KOLIBRI_FORMULA_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    FORMULA_REPRESENTATION_TEXT = 0,
    FORMULA_REPRESENTATION_ANALYTIC = 1
} FormulaRepresentation;

typedef enum {
    FORMULA_LINEAR,
    FORMULA_POLYNOMIAL,
    FORMULA_EXPONENTIAL,
    FORMULA_TRIGONOMETRIC,
    FORMULA_COMPOSITE
} FormulaType;

typedef struct {
    char id[64];
    double effectiveness;
    time_t created_at;
    uint32_t tests_passed;
    uint32_t confirmations;
    FormulaRepresentation representation;
    char content[1024];
    FormulaType type;
    double *coefficients;
    size_t coeff_count;
    char *expression;
} Formula;

typedef struct {
    Formula *formulas;
    size_t count;
    size_t capacity;
    size_t best_indices[2];
    size_t best_count;
} FormulaCollection;

#endif
