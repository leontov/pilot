#include "formula.h"

#include <stdlib.h>
#include <string.h>

#ifndef WEAK_ATTR
# if defined(__GNUC__)
#  define WEAK_ATTR __attribute__((weak))
# else
#  define WEAK_ATTR
# endif
#endif

void formula_clear(Formula *formula) WEAK_ATTR;
int formula_copy(Formula *dest, const Formula *src) WEAK_ATTR;

void formula_clear(Formula *formula) {
    if (!formula) {
        return;
    }

    free(formula->coefficients);
    formula->coefficients = NULL;
    formula->coeff_count = 0;

    free(formula->expression);
    formula->expression = NULL;
}

int formula_copy(Formula *dest, const Formula *src) {
    if (!dest || !src) {
        return -1;
    }

    formula_clear(dest);
    memset(dest, 0, sizeof(*dest));

    memcpy(dest->id, src->id, sizeof(dest->id));
    dest->effectiveness = src->effectiveness;
    dest->created_at = src->created_at;
    dest->tests_passed = src->tests_passed;
    dest->confirmations = src->confirmations;
    dest->representation = src->representation;
    dest->type = src->type;

    if (src->representation == FORMULA_REPRESENTATION_TEXT) {
        strncpy(dest->content, src->content, sizeof(dest->content) - 1);
        dest->content[sizeof(dest->content) - 1] = '\0';
    } else if (src->representation == FORMULA_REPRESENTATION_ANALYTIC) {
        dest->coeff_count = src->coeff_count;
        if (src->coeff_count > 0 && src->coefficients) {
            dest->coefficients = malloc(sizeof(double) * src->coeff_count);
            if (!dest->coefficients) {
                formula_clear(dest);
                return -1;
            }
            memcpy(dest->coefficients, src->coefficients, sizeof(double) * src->coeff_count);
        }

        if (src->expression) {
            size_t len = strlen(src->expression);
            dest->expression = malloc(len + 1);
            if (!dest->expression) {
                formula_clear(dest);
                return -1;
            }
            memcpy(dest->expression, src->expression, len + 1);
        }
    }

    return 0;
}

