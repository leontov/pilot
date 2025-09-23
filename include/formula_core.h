#ifndef FORMULA_CORE_H
#define FORMULA_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Representation of the formula payload so subsystems can reason about
// whether textual content or analytic coefficients are populated.
typedef enum {
    FORMULA_REPRESENTATION_TEXT = 0,
    FORMULA_REPRESENTATION_ANALYTIC = 1
} FormulaRepresentation;

// Shared analytic types used by advanced formula routines.
typedef enum {
    FORMULA_LINEAR,
    FORMULA_POLYNOMIAL,
    FORMULA_EXPONENTIAL,
    FORMULA_TRIGONOMETRIC,
    FORMULA_COMPOSITE
} FormulaType;

// Unified formula structure combining metadata with representation-specific
// fields. Textual content resides in the fixed buffer, while analytic
// formulas use the dynamic coefficient and expression storage.
typedef struct {
    char id[64];
    double effectiveness;
    time_t created_at;
    uint32_t tests_passed;
    uint32_t confirmations;
    FormulaRepresentation representation;

    // Text representation (used when representation == FORMULA_REPRESENTATION_TEXT)
    char content[1024];

    // Analytic representation (used when representation == FORMULA_REPRESENTATION_ANALYTIC)
    FormulaType type;
    double* coefficients;
    size_t coeff_count;
    char* expression;
} Formula;

// Collection container shared by AI and blockchain subsystems.
typedef struct {
    Formula* formulas;
    size_t count;
    size_t capacity;
} FormulaCollection;

#endif // FORMULA_CORE_H
