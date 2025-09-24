/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "synthesis/formula_vm_eval.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} byte_buffer_t;

static int bb_reserve(byte_buffer_t *bb, size_t extra) {
    if (!bb) {
        return -1;
    }
    size_t needed = bb->len + extra;
    if (needed <= bb->cap) {
        return 0;
    }
    size_t new_cap = bb->cap ? bb->cap : 16;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    uint8_t *tmp = realloc(bb->data, new_cap);
    if (!tmp) {
        return -1;
    }
    bb->data = tmp;
    bb->cap = new_cap;
    return 0;
}

static int bb_push(byte_buffer_t *bb, uint8_t value) {
    if (!bb) {
        return -1;
    }
    if (bb_reserve(bb, 1) != 0) {
        return -1;
    }
    bb->data[bb->len++] = value;
    return 0;
}

static int emit_push_number(byte_buffer_t *bb, uint64_t value) {
    if (!bb) {
        return -1;
    }
    if (bb_push(bb, 0x01) != 0 || bb_push(bb, 0x00) != 0) {
        return -1;
    }
    char digits[32];
    snprintf(digits, sizeof(digits), "%llu", (unsigned long long)value);
    for (size_t i = 0; digits[i]; ++i) {
        if (bb_push(bb, 0x01) != 0 || bb_push(bb, 0x02) != 0) {
            return -1;
        }
        if (bb_push(bb, 0x01) != 0 || bb_push(bb, 0x05) != 0) {
            return -1;
        }
        if (bb_push(bb, 0x04) != 0) {
            return -1;
        }
        if (bb_push(bb, 0x04) != 0) {
            return -1;
        }
        uint8_t digit = (uint8_t)(digits[i] - '0');
        if (bb_push(bb, 0x01) != 0 || bb_push(bb, digit) != 0) {
            return -1;
        }
        if (bb_push(bb, 0x02) != 0) {
            return -1;
        }
    }
    return 0;
}

static int compile_digits_sequence(const uint8_t *digits, size_t len, byte_buffer_t *bb) {
    if (!digits || len == 0 || !bb) {
        return -1;
    }
    char expr[128];
    size_t expr_len = 0;
    for (size_t i = 0; i < len && expr_len + 1 < sizeof(expr); ++i) {
        uint8_t v = digits[i];
        if (v <= 9) {
            expr[expr_len++] = (char)('0' + v);
        } else if (v == '+' || v == '-' || v == '*' || v == '/') {
            expr[expr_len++] = (char)v;
        }
    }
    expr[expr_len] = '\0';
    if (expr_len == 0) {
        return -1;
    }
    char *op_ptr = NULL;
    char ops[] = "+-*/";
    for (char *p = expr; *p; ++p) {
        if (strchr(ops, *p)) {
            op_ptr = p;
            break;
        }
    }
    if (!op_ptr) {
        return -1;
    }
    char op = *op_ptr;
    *op_ptr = '\0';
    uint64_t lhs = strtoull(expr, NULL, 10);
    uint64_t rhs = strtoull(op_ptr + 1, NULL, 10);

    if (emit_push_number(bb, lhs) != 0) {
        return -1;
    }
    if (emit_push_number(bb, rhs) != 0) {
        return -1;
    }

    switch (op) {
    case '+':
        if (bb_push(bb, 0x02) != 0) {
            return -1;
        }
        break;
    case '-':
        if (bb_push(bb, 0x03) != 0) {
            return -1;
        }
        break;
    case '*':
        if (bb_push(bb, 0x04) != 0) {
            return -1;
        }
        break;
    case '/':
        if (bb_push(bb, 0x05) != 0) {
            return -1;
        }
        break;
    default:
        return -1;
    }

    if (bb_push(bb, 0x12) != 0) {
        return -1;
    }
    return 0;
}

int formula_vm_compile_from_digits(const uint8_t *digits,
                                   size_t len,
                                   uint8_t **out_code,
                                   size_t *out_len) {
    if (!out_code || !out_len) {
        return -1;
    }
    *out_code = NULL;
    *out_len = 0;
    byte_buffer_t bb = {0};
    if (compile_digits_sequence(digits, len, &bb) != 0) {
        free(bb.data);
        return -1;
    }
    *out_code = bb.data;
    *out_len = bb.len;
    return 0;
}

int formula_vm_compile_from_text(const char *expression,
                                 uint8_t **out_code,
                                 size_t *out_len) {
    if (!expression) {
        return -1;
    }
    uint8_t digits[128];
    size_t digits_len = 0;
    for (const char *p = expression; *p && digits_len < sizeof(digits); ++p) {
        unsigned char c = (unsigned char)*p;
        if (isdigit(c)) {
            digits[digits_len++] = (uint8_t)(c - '0');
        } else if (c == '+' || c == '-' || c == '*' || c == '/') {
            digits[digits_len++] = c;
        }
    }
    if (digits_len == 0) {
        return -1;
    }
    return formula_vm_compile_from_digits(digits, digits_len, out_code, out_len);
}

static double compute_poe(const vm_result_t *result, size_t program_len) {
    if (!result || result->status != VM_OK || !result->halted) {
        return 0.0;
    }
    double steps_penalty = 1.0 / (1.0 + (double)result->steps / 16.0);
    double magnitude = log1p((double)result->result);
    double magnitude_norm = magnitude / (magnitude + 4.0);
    double poe = steps_penalty * magnitude_norm;
    if (poe > 1.0) {
        poe = 1.0;
    }
    if (poe < 0.0) {
        poe = 0.0;
    }
    if (program_len > 0) {
        double brevity_bonus = 1.0 / (1.0 + (double)program_len / 32.0);
        poe = fmin(1.0, poe * 0.7 + brevity_bonus * 0.3);
    }
    return poe;
}

static double compute_mdl(size_t program_len) {
    if (program_len == 0) {
        return 0.0;
    }
    double scaled = log1p((double)program_len);
    double denom = log1p(512.0);
    if (denom <= 0.0) {
        denom = 1.0;
    }
    double mdl = scaled / denom;
    if (mdl > 1.0) {
        mdl = 1.0;
    }
    return mdl;
}

int evaluate_formula_with_vm(const Formula *formula,
                             vm_result_t *out_result,
                             double *out_poe,
                             double *out_mdl,
                             size_t *out_program_len) {
    if (!formula) {
        return -1;
    }
    const char *expression = NULL;
    if (formula->representation == FORMULA_REPRESENTATION_TEXT) {
        expression = formula->content;
    } else if (formula->representation == FORMULA_REPRESENTATION_ANALYTIC) {
        expression = formula->expression;
    }
    if (!expression || strlen(expression) == 0) {
        return -1;
    }

    uint8_t *bytecode = NULL;
    size_t bytecode_len = 0;
    if (formula_vm_compile_from_text(expression, &bytecode, &bytecode_len) != 0) {
        return -1;
    }

    prog_t prog = {bytecode, bytecode_len};
    vm_limits_t limits = {256, 64};
    vm_result_t local_result = {0};
    int rc = vm_run(&prog, &limits, NULL, &local_result);
    if (out_result) {
        *out_result = local_result;
    }
    double poe = compute_poe(&local_result, bytecode_len);
    double mdl = compute_mdl(bytecode_len);
    if (out_poe) {
        *out_poe = poe;
    }
    if (out_mdl) {
        *out_mdl = mdl;
    }
    if (out_program_len) {
        *out_program_len = bytecode_len;
    }
    free(bytecode);
    return rc;
}
