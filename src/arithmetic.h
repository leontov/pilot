/* Simple safe arithmetic evaluator API */
#ifndef ARITHMETIC_H
#define ARITHMETIC_H

#include <stddef.h>

// Evaluate simple arithmetic expression in `task`, write textual result into out.
// Returns 1 if successfully parsed and evaluated, 0 otherwise.
int evaluate_arithmetic(const char *task, char *out, size_t out_sz);

#endif // ARITHMETIC_H
