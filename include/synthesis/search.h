/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_SYNTHESIS_SEARCH_H
#define KOLIBRI_SYNTHESIS_SEARCH_H

#include <stddef.h>
#include <stdint.h>

#include "formula.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t max_candidates;
    uint32_t max_terms;
    uint32_t max_coefficient;
    uint32_t max_formula_length;
    double base_effectiveness;
} FormulaSearchConfig;

FormulaSearchConfig formula_search_config_default(void);

typedef int (*formula_search_emit_fn)(const Formula *formula, void *user_data);

size_t formula_search_enumerate(const FormulaCollection *library,
                                const FormulaMemorySnapshot *memory,
                                const FormulaSearchConfig *config,
                                formula_search_emit_fn emit,
                                void *user_data);

#ifdef __cplusplus
}
#endif

#endif
