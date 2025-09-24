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

#define VM_CALL_STACK_MAX 32

typedef struct vm_context_s {
    prog_t program;
    vm_limits_t limits;
    vm_trace_t *trace;
    vm_result_t *result;
    int64_t *stack;
    size_t stack_capacity;
    size_t stack_slot;
    size_t sp;
    uint32_t ip;
    uint32_t steps;
    uint16_t call_stack[VM_CALL_STACK_MAX];
    size_t call_sp;
    vm_status_t status;
    uint8_t halted;
    uint8_t finished;
    uint32_t priority;
    uint64_t enqueue_seq;
} vm_context_t;

typedef struct vm_scheduler_s {
    vm_context_t **ready_queue;
    size_t ready_count;
    size_t ready_capacity;
    vm_context_t **all_contexts;
    size_t context_count;
    size_t context_capacity;
    int64_t *stack_pool;
    uint8_t *stack_pool_used;
    size_t stack_pool_size;
    size_t stack_capacity;
    uint32_t gas_quantum;
    size_t max_contexts;
    uint64_t next_enqueue_seq;
} vm_scheduler_t;

void vm_set_seed(uint32_t seed);

int vm_run(const prog_t *p, const vm_limits_t *lim, vm_trace_t *trace, vm_result_t *out);

int vm_scheduler_init(vm_scheduler_t *sched,
                      size_t stack_pool_size,
                      size_t stack_capacity,
                      uint32_t gas_quantum,
                      size_t max_contexts);
void vm_scheduler_destroy(vm_scheduler_t *sched);

int vm_scheduler_spawn(vm_scheduler_t *sched,
                       const prog_t *prog,
                       const vm_limits_t *limits,
                       uint32_t priority,
                       vm_trace_t *trace,
                       vm_result_t *result,
                       vm_context_t **out_ctx);

int vm_scheduler_step(vm_scheduler_t *sched);
int vm_scheduler_run(vm_scheduler_t *sched);
size_t vm_scheduler_ready_count(const vm_scheduler_t *sched);

int vm_context_finished(const vm_context_t *ctx);
vm_status_t vm_context_status(const vm_context_t *ctx);
uint32_t vm_context_gas_left(const vm_context_t *ctx);
void vm_scheduler_release(vm_scheduler_t *sched, vm_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
