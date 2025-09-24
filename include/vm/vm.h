/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_VM_VM_H
#define KOLIBRI_VM_VM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *code;
    size_t len;
} prog_t;

typedef struct {
    uint32_t max_steps;
    uint32_t max_stack;
} vm_limits_t;

typedef struct {
    uint32_t step;
    uint32_t ip;
    uint8_t opcode;
    uint64_t stack_top;
    uint32_t gas_left;
} vm_trace_entry_t;

typedef struct {
    vm_trace_entry_t *entries;
    size_t capacity;
    size_t count;
    size_t cursor;
} vm_trace_t;

typedef enum {
    VM_OK = 0,
    VM_ERR_INVALID_OPCODE = -1,
    VM_ERR_STACK_OVERFLOW = -2,
    VM_ERR_STACK_UNDERFLOW = -3,
    VM_ERR_DIV_BY_ZERO = -4,
    VM_ERR_GAS_EXHAUSTED = -5
} vm_status_t;

typedef struct {
    vm_status_t status;
    uint64_t result;
    uint32_t steps;
    uint8_t halted;
} vm_result_t;

void vm_set_seed(uint32_t seed);

int vm_run(const prog_t *p, const vm_limits_t *lim, vm_trace_t *trace, vm_result_t *out);

void vm_force_fkv_errors(int get_enabled, int get_rc, int put_enabled, int put_rc);
void vm_reset_fkv_errors(void);

#ifdef __cplusplus
}
#endif

#endif
