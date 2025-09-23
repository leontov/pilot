#ifndef FORMULA_H
#define FORMULA_H

#include "formula_core.h"

// High level categories used by legacy evaluators.
extern const int FORMULA_TYPE_SIMPLE;
extern const int FORMULA_TYPE_POLYNOMIAL;
extern const int FORMULA_TYPE_COMPOSITE;
extern const int FORMULA_TYPE_PERIODIC;

// Lifecycle helpers for working with the unified structure.
void formula_clear(Formula* formula);
int formula_copy(Formula* dest, const Formula* src);

// Collection helpers used by the Kolibri AI subsystem.
FormulaCollection* formula_collection_create(size_t initial_capacity);
void formula_collection_destroy(FormulaCollection* collection);
int formula_collection_add(FormulaCollection* collection, const Formula* formula);
Formula* formula_collection_find(FormulaCollection* collection, const char* id);

// Text-based formula utilities.
int get_formula_type(const char* content);
int validate_formula(const Formula* formula);
double evaluate_effectiveness(const Formula* formula);
Formula* generate_random_formula(int complexity_level);
char* serialize_formula(const Formula* formula);
Formula* deserialize_formula(const char* json);

#endif // FORMULA_H
