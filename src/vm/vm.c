/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "vm/vm.h"

#include "fkv/fkv.h"
#include "util/log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CALL_STACK_MAX 32

static uint32_t lcg_state = 1337u;

void vm_set_seed(uint32_t seed) {
    lcg_state = seed;
}

static uint64_t current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
}

static void trace_add(vm_trace_t *trace, uint32_t step, uint32_t ip, uint8_t opcode, int64_t stack_top, uint32_t gas_left) {
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

static int64_t pop(int64_t *stack, size_t *sp) {
    if (*sp == 0) {
        return 0;
    }
    return stack[--(*sp)];
}

static int push(int64_t *stack, size_t *sp, size_t max_stack, int64_t v) {
    if (*sp >= max_stack) {
        return -1;
    }
    stack[(*sp)++] = v;
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

int vm_run(const prog_t *p, const vm_limits_t *lim, vm_trace_t *trace, vm_result_t *out) {
    if (!p || !p->code || p->len == 0 || !lim || !out) {
        errno = EINVAL;
        return -1;
    }
    uint32_t max_steps = lim->max_steps ? lim->max_steps : 1024;
    uint32_t max_stack = lim->max_stack ? lim->max_stack : 128;

    int64_t *stack = calloc(max_stack, sizeof(int64_t));
    if (!stack) {
        return -1;
    }

    uint32_t ip = 0;
    size_t sp = 0;
    uint32_t steps = 0;
    uint16_t call_stack[CALL_STACK_MAX];
    size_t call_sp = 0;
    vm_status_t status = VM_OK;
    uint8_t halted = 0;

    if (trace) {
        trace->count = 0;
    }

    while (ip < p->len) {
        if (steps >= max_steps) {
            status = VM_ERR_GAS_EXHAUSTED;
            break;
        }
        uint8_t opcode = p->code[ip++];
        int64_t before_top = (sp > 0) ? stack[sp - 1] : 0;
        trace_add(trace, steps, ip - 1, opcode, before_top, max_steps - steps);
        steps++;

        switch (opcode) {
        case 0x01: { // PUSHd
            if (ip >= p->len) {
                status = VM_ERR_INVALID_OPCODE;
                goto done;
            }
            uint8_t digit = p->code[ip++];
            if (push(stack, &sp, max_stack, (int64_t)digit) != 0) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            break;
        }
        case 0x02: { // ADD10
            if (sp < 2) {
                status = VM_ERR_STACK_UNDERFLOW;
                goto done;
            }
            int64_t b = pop(stack, &sp);
            int64_t a = pop(stack, &sp);
            if (push(stack, &sp, max_stack, a + b) != 0) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            break;
        }
        case 0x03: { // SUB10
            if (sp < 2) {
                status = VM_ERR_STACK_UNDERFLOW;
                goto done;
            }
            int64_t b = pop(stack, &sp);
            int64_t a = pop(stack, &sp);
            if (push(stack, &sp, max_stack, a - b) != 0) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            break;
        }
        case 0x04: { // MUL10
            if (sp < 2) {
                status = VM_ERR_STACK_UNDERFLOW;
                goto done;
            }
            int64_t b = pop(stack, &sp);
            int64_t a = pop(stack, &sp);
            if (push(stack, &sp, max_stack, a * b) != 0) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            break;
        }
        case 0x05: { // DIV10
            if (sp < 2) {
                status = VM_ERR_STACK_UNDERFLOW;
                goto done;
            }
            int64_t b = pop(stack, &sp);
            int64_t a = pop(stack, &sp);
            if (b == 0) {
                status = VM_ERR_DIV_BY_ZERO;
                goto done;
            }
            if (push(stack, &sp, max_stack, a / b) != 0) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            break;
        }
        case 0x06: { // MOD10
            if (sp < 2) {
                status = VM_ERR_STACK_UNDERFLOW;
                goto done;
            }
            int64_t b = pop(stack, &sp);
            int64_t a = pop(stack, &sp);
            if (b == 0) {
                status = VM_ERR_DIV_BY_ZERO;
                goto done;
            }
            if (push(stack, &sp, max_stack, a % b) != 0) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            break;
        }
        case 0x07: { // CMP
            if (sp < 2) {
                status = VM_ERR_STACK_UNDERFLOW;
                goto done;
            }
            int64_t b = pop(stack, &sp);
            int64_t a = pop(stack, &sp);
            int64_t res = 0;
            if (a < b) {
                res = -1;
            } else if (a > b) {
                res = 1;
            }
            if (push(stack, &sp, max_stack, res) != 0) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            break;
        }
        case 0x08: // JZ
        case 0x09: { // JNZ
            if (ip + 1 >= p->len) {
                status = VM_ERR_INVALID_OPCODE;
                goto done;
            }
            uint16_t rel = (uint16_t)p->code[ip] | ((uint16_t)p->code[ip + 1] << 8);
            ip += 2;
            int16_t offset = (int16_t)rel;
            if (sp == 0) {
                status = VM_ERR_STACK_UNDERFLOW;
                goto done;
            }
            int64_t value = pop(stack, &sp);
            int jump = ((opcode == 0x08) && value == 0) || ((opcode == 0x09) && value != 0);
            if (jump) {
                int64_t new_ip = (int64_t)ip + offset;
                if (new_ip < 0 || new_ip > (int64_t)p->len) {
                    status = VM_ERR_INVALID_OPCODE;
                    goto done;
                }
                ip = (uint32_t)new_ip;
            }
            break;
        }
        case 0x0A: { // CALL
            if (ip + 1 >= p->len) {
                status = VM_ERR_INVALID_OPCODE;
                goto done;
            }
            if (call_sp >= CALL_STACK_MAX) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            uint16_t addr = (uint16_t)p->code[ip] | ((uint16_t)p->code[ip + 1] << 8);
            ip += 2;
            call_stack[call_sp++] = ip;
            if (addr >= p->len) {
                status = VM_ERR_INVALID_OPCODE;
                goto done;
            }
            ip = addr;
            break;
        }
        case 0x0B: { // RET
            if (call_sp == 0) {
                status = VM_OK;
                goto done;
            }
            ip = call_stack[--call_sp];
            break;
        }
        case 0x0C: { // READ_FKV
            if (sp == 0) {
                status = VM_ERR_STACK_UNDERFLOW;
                goto done;
            }
            uint64_t key_num = (uint64_t)pop(stack, &sp);
            uint8_t key_digits[32];
            size_t key_len = sizeof(key_digits);
            if (number_to_digits(key_num, key_digits, &key_len) != 0) {
                status = VM_ERR_INVALID_OPCODE;
                goto done;
            }
            fkv_iter_t it = {0};
            if (fkv_get_prefix(key_digits, key_len, &it, 1) != 0 || it.count == 0) {
                fkv_iter_free(&it);
                if (push(stack, &sp, max_stack, 0) != 0) {
                    status = VM_ERR_STACK_OVERFLOW;
                    goto done;
                }
                break;
            }
            uint64_t value = 0;
            for (size_t i = 0; i < it.entries[0].value_len; ++i) {
                value = value * 10 + it.entries[0].value[i];
            }
            fkv_iter_free(&it);
            if (push(stack, &sp, max_stack, (int64_t)value) != 0) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            break;
        }
        case 0x0D: { // WRITE_FKV
            if (sp < 2) {
                status = VM_ERR_STACK_UNDERFLOW;
                goto done;
            }
            uint64_t value_num = (uint64_t)pop(stack, &sp);
            uint64_t key_num = (uint64_t)pop(stack, &sp);
            uint8_t key_digits[32];
            size_t key_len = sizeof(key_digits);
            uint8_t value_digits[32];
            size_t value_len = sizeof(value_digits);
            if (number_to_digits(key_num, key_digits, &key_len) != 0 ||
                number_to_digits(value_num, value_digits, &value_len) != 0) {
                status = VM_ERR_INVALID_OPCODE;
                goto done;
            }
            fkv_put(key_digits, key_len, value_digits, value_len, FKV_ENTRY_TYPE_VALUE);
            break;
        }
        case 0x0E: { // HASH10
            if (sp == 0) {
                status = VM_ERR_STACK_UNDERFLOW;
                goto done;
            }
            uint64_t value = (uint64_t)pop(stack, &sp);
            uint64_t hash = value * 2654435761u;
            if (push(stack, &sp, max_stack, (int64_t)(hash % 10000000000ull)) != 0) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            break;
        }
        case 0x0F: { // RANDOM10
            lcg_state = 1664525u * lcg_state + 1013904223u;
            uint64_t rnd = (uint64_t)lcg_state % 10000000000ull;
            if (push(stack, &sp, max_stack, (int64_t)rnd) != 0) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            break;
        }
        case 0x10: { // TIME10
            uint64_t t = current_time_ms();
            if (push(stack, &sp, max_stack, (int64_t)t) != 0) {
                status = VM_ERR_STACK_OVERFLOW;
                goto done;
            }
            break;
        }
        case 0x11: // NOP
            break;
        case 0x12: { // HALT
            status = VM_OK;
            halted = 1;
            goto done;
        }
        default:
            status = VM_ERR_INVALID_OPCODE;
            goto done;
        }
    }

done:
    if (out) {
        out->status = status;
        out->steps = steps;
        out->result = (sp > 0) ? (uint64_t)stack[sp - 1] : 0;
        out->halted = halted;
    }
    free(stack);
    return 0;
}
