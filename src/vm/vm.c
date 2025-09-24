/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#define _POSIX_C_SOURCE 200809L

#include "vm/vm.h"

#include "fkv/fkv.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VM_DEFAULT_MAX_STEPS 1024u
#define VM_DEFAULT_MAX_STACK 128u

static uint32_t lcg_state = 1337u;

void vm_set_seed(uint32_t seed) {
    lcg_state = seed;
}

static uint64_t current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
}

static void trace_add(vm_trace_t *trace,
                      uint32_t step,
                      uint32_t ip,
                      uint8_t opcode,
                      int64_t stack_top,
                      uint32_t gas_left) {
    if (!trace || !trace->entries || trace->capacity == 0) {
        return;
    }
    if (trace->count >= trace->capacity) {
        return;
    }
    vm_trace_entry_t *entry = &trace->entries[trace->count++];
    entry->step = step;
    entry->ip = ip;
    entry->opcode = opcode;
    entry->stack_top = stack_top;
    entry->gas_left = gas_left;
}

static int64_t ctx_pop(vm_context_t *ctx, int *err) {
    if (ctx->sp == 0) {
        if (err) {
            *err = 1;
        }
        return 0;
    }
    if (err) {
        *err = 0;
    }
    return ctx->stack[--ctx->sp];
}

static int ctx_push(vm_context_t *ctx, int64_t value) {
    if (ctx->sp >= ctx->stack_capacity) {
        return -1;
    }
    ctx->stack[ctx->sp++] = value;
    return 0;
}

static int number_to_digits(uint64_t value, uint8_t *digits, size_t *len) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    size_t n = strlen(buf);
    if (n > *len) {
        return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        digits[i] = (uint8_t)(buf[i] - '0');
    }
    *len = n;
    return 0;
}

static void vm_context_finalize(vm_context_t *ctx) {
    if (!ctx || !ctx->result) {
        return;
    }
    ctx->result->status = ctx->status;
    ctx->result->steps = ctx->steps;
    ctx->result->result = (ctx->sp > 0) ? (uint64_t)ctx->stack[ctx->sp - 1] : 0;
    ctx->result->halted = ctx->halted;
}

static uint32_t effective_gas_left(const vm_context_t *ctx) {
    uint32_t limit = ctx->limits.max_steps ? ctx->limits.max_steps : VM_DEFAULT_MAX_STEPS;
    if (ctx->steps >= limit) {
        return 0;
    }
    return limit - ctx->steps;
}

static int vm_context_execute_slice(vm_context_t *ctx, uint32_t quantum) {
    if (!ctx || !ctx->stack || ctx->finished) {
        return 0;
    }
    if (quantum == 0) {
        quantum = 1;
    }
    uint32_t executed = 0;

    while (!ctx->finished && executed < quantum) {
        if (ctx->steps >= ctx->limits.max_steps) {
            ctx->status = VM_ERR_GAS_EXHAUSTED;
            ctx->finished = 1;
            break;
        }
        if (ctx->ip >= ctx->program.len) {
            ctx->finished = 1;
            break;
        }

        uint8_t opcode = ctx->program.code[ctx->ip++];
        int64_t before_top = (ctx->sp > 0) ? ctx->stack[ctx->sp - 1] : 0;
        uint32_t gas_left = effective_gas_left(ctx);
        trace_add(ctx->trace, ctx->steps, ctx->ip - 1, opcode, before_top, gas_left);
        ctx->steps++;
        executed++;

        switch (opcode) {
        case 0x01: { // PUSHd
            if (ctx->ip >= ctx->program.len) {
                ctx->status = VM_ERR_INVALID_OPCODE;
                ctx->finished = 1;
                goto finish;
            }
            uint8_t digit = ctx->program.code[ctx->ip++];
            if (ctx_push(ctx, (int64_t)digit) != 0) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            break;
        }
        case 0x02: { // ADD10
            int err = 0;
            int64_t b = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            int64_t a = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            if (ctx_push(ctx, a + b) != 0) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            break;
        }
        case 0x03: { // SUB10
            int err = 0;
            int64_t b = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            int64_t a = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            if (ctx_push(ctx, a - b) != 0) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            break;
        }
        case 0x04: { // MUL10
            int err = 0;
            int64_t b = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            int64_t a = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            if (ctx_push(ctx, a * b) != 0) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            break;
        }
        case 0x05: { // DIV10
            int err = 0;
            int64_t b = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            int64_t a = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            if (b == 0) {
                ctx->status = VM_ERR_DIV_BY_ZERO;
                ctx->finished = 1;
                goto finish;
            }
            if (ctx_push(ctx, a / b) != 0) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            break;
        }
        case 0x06: { // MOD10
            int err = 0;
            int64_t b = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            int64_t a = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            if (b == 0) {
                ctx->status = VM_ERR_DIV_BY_ZERO;
                ctx->finished = 1;
                goto finish;
            }
            if (ctx_push(ctx, a % b) != 0) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            break;
        }
        case 0x07: { // CMP
            int err = 0;
            int64_t b = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            int64_t a = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            int64_t res = 0;
            if (a < b) {
                res = -1;
            } else if (a > b) {
                res = 1;
            }
            if (ctx_push(ctx, res) != 0) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            break;
        }
        case 0x08: // JZ
        case 0x09: { // JNZ
            if (ctx->ip + 1 >= ctx->program.len) {
                ctx->status = VM_ERR_INVALID_OPCODE;
                ctx->finished = 1;
                goto finish;
            }
            uint16_t rel = (uint16_t)ctx->program.code[ctx->ip] |
                           ((uint16_t)ctx->program.code[ctx->ip + 1] << 8);
            ctx->ip += 2;
            int err = 0;
            int64_t value = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            int jump = ((opcode == 0x08) && value == 0) || ((opcode == 0x09) && value != 0);
            if (jump) {
                int16_t offset = (int16_t)rel;
                int64_t new_ip = (int64_t)ctx->ip + offset;
                if (new_ip < 0 || new_ip > (int64_t)ctx->program.len) {
                    ctx->status = VM_ERR_INVALID_OPCODE;
                    ctx->finished = 1;
                    goto finish;
                }
                ctx->ip = (uint32_t)new_ip;
            }
            break;
        }
        case 0x0A: { // CALL
            if (ctx->ip + 1 >= ctx->program.len) {
                ctx->status = VM_ERR_INVALID_OPCODE;
                ctx->finished = 1;
                goto finish;
            }
            if (ctx->call_sp >= VM_CALL_STACK_MAX) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            uint16_t addr = (uint16_t)ctx->program.code[ctx->ip] |
                             ((uint16_t)ctx->program.code[ctx->ip + 1] << 8);
            ctx->ip += 2;
            if (addr >= ctx->program.len) {
                ctx->status = VM_ERR_INVALID_OPCODE;
                ctx->finished = 1;
                goto finish;
            }
            ctx->call_stack[ctx->call_sp++] = ctx->ip;
            ctx->ip = addr;
            break;
        }
        case 0x0B: { // RET
            if (ctx->call_sp == 0) {
                ctx->status = VM_OK;
                ctx->halted = 1;
                ctx->finished = 1;
                goto finish;
            }
            ctx->ip = ctx->call_stack[--ctx->call_sp];
            break;
        }
        case 0x0C: { // READ_FKV
            int err = 0;
            int64_t key_val = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            uint8_t key_digits[32];
            size_t key_len = sizeof(key_digits);
            if (number_to_digits((uint64_t)key_val, key_digits, &key_len) != 0) {
                ctx->status = VM_ERR_INVALID_OPCODE;
                ctx->finished = 1;
                goto finish;
            }
            fkv_iter_t it = {0};
            if (fkv_get_prefix(key_digits, key_len, &it, 1) != 0 || it.count == 0) {
                fkv_iter_free(&it);
                if (ctx_push(ctx, 0) != 0) {
                    ctx->status = VM_ERR_STACK_OVERFLOW;
                    ctx->finished = 1;
                    goto finish;
                }
                break;
            }
            uint64_t value = 0;
            for (size_t i = 0; i < it.entries[0].value_len; ++i) {
                value = value * 10 + it.entries[0].value[i];
            }
            fkv_iter_free(&it);
            if (ctx_push(ctx, (int64_t)value) != 0) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            break;
        }
        case 0x0D: { // WRITE_FKV
            int err = 0;
            int64_t value_num = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            int64_t key_num = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            uint8_t key_digits[32];
            size_t key_len = sizeof(key_digits);
            uint8_t value_digits[32];
            size_t value_len = sizeof(value_digits);
            if (number_to_digits((uint64_t)key_num, key_digits, &key_len) != 0 ||
                number_to_digits((uint64_t)value_num, value_digits, &value_len) != 0) {
                ctx->status = VM_ERR_INVALID_OPCODE;
                ctx->finished = 1;
                goto finish;
            }
            fkv_put(key_digits, key_len, value_digits, value_len, FKV_ENTRY_TYPE_VALUE);
            break;
        }
        case 0x0E: { // HASH10
            int err = 0;
            int64_t value = ctx_pop(ctx, &err);
            if (err) {
                ctx->status = VM_ERR_STACK_UNDERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            uint64_t hash = (uint64_t)value * 2654435761u;
            if (ctx_push(ctx, (int64_t)(hash % 10000000000ull)) != 0) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            break;
        }
        case 0x0F: { // RANDOM10
            lcg_state = 1664525u * lcg_state + 1013904223u;
            uint64_t rnd = (uint64_t)lcg_state % 10000000000ull;
            if (ctx_push(ctx, (int64_t)rnd) != 0) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            break;
        }
        case 0x10: { // TIME10
            uint64_t t = current_time_ms();
            if (ctx_push(ctx, (int64_t)t) != 0) {
                ctx->status = VM_ERR_STACK_OVERFLOW;
                ctx->finished = 1;
                goto finish;
            }
            break;
        }
        case 0x11: // NOP
            break;
        case 0x12: { // HALT
            ctx->status = VM_OK;
            ctx->halted = 1;
            ctx->finished = 1;
            goto finish;
        }
        default:
            ctx->status = VM_ERR_INVALID_OPCODE;
            ctx->finished = 1;
            goto finish;
        }
    }

finish:
    if (ctx->finished) {
        vm_context_finalize(ctx);
    }
    return 0;
}

static int ensure_ready_capacity(vm_scheduler_t *sched) {
    if (sched->ready_count < sched->ready_capacity) {
        return 0;
    }
    size_t new_capacity = sched->ready_capacity ? sched->ready_capacity * 2 : 8;
    vm_context_t **tmp = realloc(sched->ready_queue, new_capacity * sizeof(vm_context_t *));
    if (!tmp) {
        return -1;
    }
    sched->ready_queue = tmp;
    sched->ready_capacity = new_capacity;
    return 0;
}

static int ensure_context_capacity(vm_scheduler_t *sched) {
    if (sched->context_count < sched->context_capacity) {
        return 0;
    }
    size_t new_capacity = sched->context_capacity ? sched->context_capacity * 2 : 8;
    vm_context_t **tmp = realloc(sched->all_contexts, new_capacity * sizeof(vm_context_t *));
    if (!tmp) {
        return -1;
    }
    sched->all_contexts = tmp;
    sched->context_capacity = new_capacity;
    return 0;
}

static int ready_cmp(const vm_context_t *lhs, const vm_context_t *rhs) {
    if (lhs->priority != rhs->priority) {
        return (lhs->priority > rhs->priority) ? 1 : -1;
    }
    if (lhs->enqueue_seq != rhs->enqueue_seq) {
        return (lhs->enqueue_seq < rhs->enqueue_seq) ? 1 : -1;
    }
    return 0;
}

static void ready_swap(vm_context_t **a, vm_context_t **b) {
    vm_context_t *tmp = *a;
    *a = *b;
    *b = tmp;
}

static void ready_sift_up(vm_scheduler_t *sched, size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (ready_cmp(sched->ready_queue[idx], sched->ready_queue[parent]) <= 0) {
            break;
        }
        ready_swap(&sched->ready_queue[idx], &sched->ready_queue[parent]);
        idx = parent;
    }
}

static void ready_sift_down(vm_scheduler_t *sched, size_t idx) {
    size_t count = sched->ready_count;
    while (idx < count) {
        size_t left = idx * 2 + 1;
        size_t right = left + 1;
        size_t best = idx;

        if (left < count && ready_cmp(sched->ready_queue[left], sched->ready_queue[best]) > 0) {
            best = left;
        }
        if (right < count && ready_cmp(sched->ready_queue[right], sched->ready_queue[best]) > 0) {
            best = right;
        }

        if (best == idx) {
            break;
        }

        ready_swap(&sched->ready_queue[idx], &sched->ready_queue[best]);
        idx = best;
    }
}

static void ready_push(vm_scheduler_t *sched, vm_context_t *ctx) {
    if (!sched || !ctx) {
        return;
    }
    if (ensure_ready_capacity(sched) != 0) {
        return;
    }
    size_t idx = sched->ready_count++;
    sched->ready_queue[idx] = ctx;
    ready_sift_up(sched, idx);
}

static vm_context_t *ready_pop(vm_scheduler_t *sched) {
    if (!sched || sched->ready_count == 0) {
        return NULL;
    }
    vm_context_t *ctx = sched->ready_queue[0];
    sched->ready_count--;
    if (sched->ready_count > 0) {
        sched->ready_queue[0] = sched->ready_queue[sched->ready_count];
        sched->ready_queue[sched->ready_count] = NULL;
        ready_sift_down(sched, 0);
    } else {
        sched->ready_queue[0] = NULL;
    }
    return ctx;
}

static void release_stack(vm_scheduler_t *sched, vm_context_t *ctx) {
    if (!sched || !ctx || !ctx->stack) {
        return;
    }
    if (ctx->stack_slot < sched->stack_pool_size) {
        memset(ctx->stack, 0, sched->stack_capacity * sizeof(int64_t));
        sched->stack_pool_used[ctx->stack_slot] = 0;
    } else {
        free(ctx->stack);
    }
    ctx->stack = NULL;
    ctx->stack_slot = SIZE_MAX;
}

static int acquire_stack(vm_scheduler_t *sched, vm_context_t *ctx) {
    if (!sched || !ctx) {
        return -1;
    }
    if (ctx->stack_capacity == 0) {
        ctx->stack_capacity = VM_DEFAULT_MAX_STACK;
    }
    if (sched->stack_pool_size > 0 && ctx->stack_capacity <= sched->stack_capacity) {
        for (size_t i = 0; i < sched->stack_pool_size; ++i) {
            if (!sched->stack_pool_used[i]) {
                sched->stack_pool_used[i] = 1;
                ctx->stack_slot = i;
                ctx->stack = sched->stack_pool + (i * sched->stack_capacity);
                memset(ctx->stack, 0, sched->stack_capacity * sizeof(int64_t));
                return 0;
            }
        }
    }
    ctx->stack = calloc(ctx->stack_capacity, sizeof(int64_t));
    if (!ctx->stack) {
        ctx->stack_slot = SIZE_MAX;
        return -1;
    }
    ctx->stack_slot = SIZE_MAX;
    return 0;
}

int vm_scheduler_init(vm_scheduler_t *sched,
                      size_t stack_pool_size,
                      size_t stack_capacity,
                      uint32_t gas_quantum,
                      size_t max_contexts) {
    if (!sched) {
        errno = EINVAL;
        return -1;
    }
    memset(sched, 0, sizeof(*sched));
    sched->stack_pool_size = stack_pool_size;
    sched->stack_capacity = stack_capacity ? stack_capacity : VM_DEFAULT_MAX_STACK;
    sched->gas_quantum = gas_quantum ? gas_quantum : 1;
    sched->max_contexts = max_contexts ? max_contexts : 0;
    sched->next_enqueue_seq = 1;

    if (sched->stack_pool_size > 0) {
        sched->stack_pool = calloc(sched->stack_pool_size * sched->stack_capacity, sizeof(int64_t));
        if (!sched->stack_pool) {
            vm_scheduler_destroy(sched);
            return -1;
        }
        sched->stack_pool_used = calloc(sched->stack_pool_size, sizeof(uint8_t));
        if (!sched->stack_pool_used) {
            vm_scheduler_destroy(sched);
            return -1;
        }
    }
    return 0;
}

static void remove_from_ready(vm_scheduler_t *sched, vm_context_t *ctx) {
    if (!sched || !ctx || sched->ready_count == 0) {
        return;
    }
    size_t index = SIZE_MAX;
    for (size_t i = 0; i < sched->ready_count; ++i) {
        if (sched->ready_queue[i] == ctx) {
            index = i;
            break;
        }
    }
    if (index == SIZE_MAX) {
        return;
    }
    sched->ready_count--;
    if (index == sched->ready_count) {
        sched->ready_queue[index] = NULL;
        return;
    }
    sched->ready_queue[index] = sched->ready_queue[sched->ready_count];
    sched->ready_queue[sched->ready_count] = NULL;
    if (index > 0 && ready_cmp(sched->ready_queue[index],
                               sched->ready_queue[(index - 1) / 2]) > 0) {
        ready_sift_up(sched, index);
    } else {
        ready_sift_down(sched, index);
    }
}

void vm_scheduler_destroy(vm_scheduler_t *sched) {
    if (!sched) {
        return;
    }
    for (size_t i = 0; i < sched->context_count; ++i) {
        vm_context_t *ctx = sched->all_contexts[i];
        if (!ctx) {
            continue;
        }
        release_stack(sched, ctx);
        free(ctx);
    }
    free(sched->all_contexts);
    free(sched->ready_queue);
    if (sched->stack_pool_size > 0) {
        free(sched->stack_pool_used);
        free(sched->stack_pool);
    }
    memset(sched, 0, sizeof(*sched));
}

int vm_scheduler_spawn(vm_scheduler_t *sched,
                       const prog_t *prog,
                       const vm_limits_t *limits,
                       uint32_t priority,
                       vm_trace_t *trace,
                       vm_result_t *result,
                       vm_context_t **out_ctx) {
    if (!sched || !prog || !prog->code || prog->len == 0) {
        errno = EINVAL;
        return -1;
    }
    if (sched->max_contexts > 0 && sched->context_count >= sched->max_contexts) {
        errno = EAGAIN;
        return -1;
    }
    if (ensure_context_capacity(sched) != 0) {
        return -1;
    }

    vm_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return -1;
    }

    ctx->program = *prog;
    if (limits) {
        ctx->limits = *limits;
    }
    if (ctx->limits.max_steps == 0) {
        ctx->limits.max_steps = VM_DEFAULT_MAX_STEPS;
    }
    if (ctx->limits.max_stack == 0) {
        ctx->limits.max_stack = sched->stack_capacity ? (uint32_t)sched->stack_capacity : VM_DEFAULT_MAX_STACK;
    }
    ctx->stack_capacity = ctx->limits.max_stack;
    ctx->trace = trace;
    if (ctx->trace) {
        ctx->trace->count = 0;
    }
    ctx->result = result;
    ctx->status = VM_OK;
    ctx->halted = 0;
    ctx->finished = 0;
    ctx->priority = priority;
    ctx->enqueue_seq = sched->next_enqueue_seq++;
    ctx->stack_slot = SIZE_MAX;

    if (acquire_stack(sched, ctx) != 0) {
        free(ctx);
        return -1;
    }

    sched->all_contexts[sched->context_count++] = ctx;
    ready_push(sched, ctx);

    if (out_ctx) {
        *out_ctx = ctx;
    }
    return 0;
}

int vm_scheduler_step(vm_scheduler_t *sched) {
    if (!sched) {
        errno = EINVAL;
        return -1;
    }
    vm_context_t *ctx = ready_pop(sched);
    if (!ctx) {
        return 0;
    }

    uint32_t gas_quantum = sched->gas_quantum ? sched->gas_quantum : 1;
    uint32_t gas_left = effective_gas_left(ctx);
    if (gas_left > 0 && gas_left < gas_quantum) {
        gas_quantum = gas_left;
    }
    vm_context_execute_slice(ctx, gas_quantum);
    if (!ctx->finished && ctx->status == VM_OK) {
        ctx->enqueue_seq = sched->next_enqueue_seq++;
        ready_push(sched, ctx);
    } else if (!ctx->finished) {
        ctx->finished = 1;
        vm_context_finalize(ctx);
        release_stack(sched, ctx);
    } else {
        release_stack(sched, ctx);
    }
    return 0;
}

int vm_scheduler_run(vm_scheduler_t *sched) {
    if (!sched) {
        errno = EINVAL;
        return -1;
    }
    while (sched->ready_count > 0) {
        int rc = vm_scheduler_step(sched);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

size_t vm_scheduler_ready_count(const vm_scheduler_t *sched) {
    if (!sched) {
        return 0;
    }
    return sched->ready_count;
}

int vm_context_finished(const vm_context_t *ctx) {
    return ctx ? (ctx->finished != 0) : 0;
}

vm_status_t vm_context_status(const vm_context_t *ctx) {
    if (!ctx) {
        return VM_ERR_INVALID_OPCODE;
    }
    return ctx->status;
}

uint32_t vm_context_gas_left(const vm_context_t *ctx) {
    if (!ctx) {
        return 0;
    }
    return effective_gas_left(ctx);
}

void vm_scheduler_release(vm_scheduler_t *sched, vm_context_t *ctx) {
    if (!sched || !ctx) {
        return;
    }
    remove_from_ready(sched, ctx);
    release_stack(sched, ctx);
    size_t index = SIZE_MAX;
    for (size_t i = 0; i < sched->context_count; ++i) {
        if (sched->all_contexts[i] == ctx) {
            index = i;
            break;
        }
    }
    if (index < sched->context_count) {
        sched->context_count--;
        sched->all_contexts[index] = sched->all_contexts[sched->context_count];
        sched->all_contexts[sched->context_count] = NULL;
    }
    free(ctx);
}

int vm_run(const prog_t *p, const vm_limits_t *lim, vm_trace_t *trace, vm_result_t *out) {
    if (!p || !p->code || p->len == 0 || !out) {
        errno = EINVAL;
        return -1;
    }

    vm_limits_t local_limits = {0};
    if (lim) {
        local_limits = *lim;
    }
    if (local_limits.max_steps == 0) {
        local_limits.max_steps = VM_DEFAULT_MAX_STEPS;
    }
    if (local_limits.max_stack == 0) {
        local_limits.max_stack = VM_DEFAULT_MAX_STACK;
    }

    vm_scheduler_t sched;
    if (vm_scheduler_init(&sched,
                          1,
                          local_limits.max_stack,
                          local_limits.max_steps,
                          1) != 0) {
        return -1;
    }

    vm_context_t *ctx = NULL;
    int rc = vm_scheduler_spawn(&sched, p, &local_limits, 0, trace, out, &ctx);
    if (rc != 0) {
        vm_scheduler_destroy(&sched);
        return rc;
    }

    rc = vm_scheduler_run(&sched);
    vm_scheduler_release(&sched, ctx);
    vm_scheduler_destroy(&sched);
    return rc;
}
