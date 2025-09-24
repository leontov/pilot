/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef FORMULA_VM_EVAL_H
#define FORMULA_VM_EVAL_H

#include <stddef.h>
#include <stdint.h>

#include "formula_core.h"
#include "vm/vm.h"

#ifdef __cplusplus
extern "C" {
#endif

int formula_vm_compile_from_digits(const uint8_t *digits,
                                   size_t len,
                                   uint8_t **out_code,
                                   size_t *out_len);
int formula_vm_compile_from_text(const char *expression,
                                 uint8_t **out_code,
                                 size_t *out_len);
int evaluate_formula_with_vm(const Formula *formula,
                             vm_result_t *out_result,
                             double *out_poe,
                             double *out_mdl,
                             size_t *out_program_len);

#ifdef __cplusplus
}
#endif

#endif // FORMULA_VM_EVAL_H
