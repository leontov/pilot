/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "vm/vm.h"
#include "fkv/fkv.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct byte_buffer {
    uint8_t *data;
    size_t len;
    size_t cap;
};

static int bb_push(struct byte_buffer *bb, uint8_t byte) {
    if (bb->len + 1 > bb->cap) {
        size_t new_cap = bb->cap ? bb->cap * 2 : 32;
        uint8_t *tmp = realloc(bb->data, new_cap);
        if (!tmp) {
            return -1;
        }
        bb->data = tmp;
        bb->cap = new_cap;
    }
    bb->data[bb->len++] = byte;
    return 0;
}

static int emit_push_number(struct byte_buffer *bb, uint64_t value) {
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

static int run_program(struct byte_buffer *bb, uint64_t *result, vm_status_t *status) {
    if (bb_push(bb, 0x0B) != 0) {
        return -1;
    }
    prog_t prog = {bb->data, bb->len};
    vm_limits_t lim = {512, 128};
    vm_trace_entry_t entries[64];
    vm_trace_t trace = {entries, 64, 0, 0};
    vm_result_t out;
    int rc = vm_run(&prog, &lim, &trace, &out);
    *result = out.result;
    *status = out.status;
    return rc;
}

static void test_random_deterministic(void) {
    struct byte_buffer bb = {0};
    vm_set_seed(42);
    assert(bb_push(&bb, 0x0F) == 0);
    uint64_t result = 0;
    vm_status_t status;
    assert(run_program(&bb, &result, &status) == 0);
    assert(status == VM_OK);
    assert(result == 1083814273ull);
    free(bb.data);
}

static void test_add(void) {
    struct byte_buffer bb = {0};
    assert(emit_push_number(&bb, 2) == 0);
    assert(emit_push_number(&bb, 2) == 0);
    assert(bb_push(&bb, 0x02) == 0);
    uint64_t result = 0;
    vm_status_t status;
    assert(run_program(&bb, &result, &status) == 0);
    assert(status == VM_OK);
    assert(result == 4);
    free(bb.data);
}

static void test_mul(void) {
    struct byte_buffer bb = {0};
    assert(emit_push_number(&bb, 126) == 0);
    assert(emit_push_number(&bb, 6) == 0);
    assert(bb_push(&bb, 0x04) == 0);
    uint64_t result = 0;
    vm_status_t status;
    assert(run_program(&bb, &result, &status) == 0);
    assert(status == VM_OK);
    assert(result == 756);
    free(bb.data);
}

static void test_div_zero(void) {
    struct byte_buffer bb = {0};
    assert(emit_push_number(&bb, 8) == 0);
    assert(emit_push_number(&bb, 0) == 0);
    assert(bb_push(&bb, 0x05) == 0);
    uint64_t result = 0;
    vm_status_t status;
    assert(run_program(&bb, &result, &status) == 0);
    assert(status == VM_ERR_DIV_BY_ZERO);
    free(bb.data);
}

static void test_halt(void) {
    uint8_t code[] = {0x01, 0x05, 0x12, 0x01, 0x09};
    prog_t prog = {code, sizeof(code)};
    vm_limits_t lim = {512, 128};
    vm_trace_entry_t entries[8];
    vm_trace_t trace = {entries, 8, 0, 0};
    vm_result_t out;
    assert(vm_run(&prog, &lim, &trace, &out) == 0);
    assert(out.status == VM_OK);
    assert(out.halted == 1);
    assert(out.steps == 2);
    assert(out.result == 5);
    assert(trace.count == 2);
    assert(trace.entries[1].opcode == 0x12);
}

static void test_read_fkv_negative_operand(void) {
    struct byte_buffer bb = {0};
    assert(fkv_init() == 0);

    uint8_t key_digits[] = {4};
    uint8_t value_digits[] = {2, 1};
    assert(fkv_put(key_digits, sizeof(key_digits), value_digits, sizeof(value_digits), FKV_ENTRY_TYPE_VALUE) == 0);

    assert(bb_push(&bb, 0x01) == 0 && bb_push(&bb, 0x00) == 0); // push 0
    assert(bb_push(&bb, 0x01) == 0 && bb_push(&bb, 0x01) == 0); // push 1
    assert(bb_push(&bb, 0x03) == 0); // SUB10 -> -1
    assert(bb_push(&bb, 0x0C) == 0); // READ_FKV

    uint64_t result = 0;
    vm_status_t status;
    assert(run_program(&bb, &result, &status) == 0);
    assert(status == VM_ERR_INVALID_OPCODE);
    assert(result == UINT64_MAX);

    fkv_iter_t it = {0};
    assert(fkv_get_prefix(key_digits, sizeof(key_digits), &it, 1) == 0);
    assert(it.count == 1);
    assert(it.entries[0].value_len == sizeof(value_digits));
    assert(memcmp(it.entries[0].value, value_digits, sizeof(value_digits)) == 0);
    fkv_iter_free(&it);

    fkv_shutdown();
    free(bb.data);
}

static void test_write_fkv_negative_operand(void) {
    struct byte_buffer bb = {0};
    assert(fkv_init() == 0);

    assert(bb_push(&bb, 0x01) == 0 && bb_push(&bb, 0x00) == 0); // push 0
    assert(bb_push(&bb, 0x01) == 0 && bb_push(&bb, 0x01) == 0); // push 1
    assert(bb_push(&bb, 0x03) == 0); // SUB10 -> -1 (key)
    assert(emit_push_number(&bb, 7) == 0); // value
    assert(bb_push(&bb, 0x0D) == 0); // WRITE_FKV

    uint64_t result = 0;
    vm_status_t status;
    assert(run_program(&bb, &result, &status) == 0);
    assert(status == VM_ERR_INVALID_OPCODE);
    assert(result == 7);

    uint8_t probe_key[] = {7};
    fkv_iter_t it = {0};
    assert(fkv_get_prefix(probe_key, sizeof(probe_key), &it, 1) == 0);
    assert(it.count == 0);
    fkv_iter_free(&it);

    fkv_shutdown();
    free(bb.data);
}

int main(void) {
    test_random_deterministic();
    test_add();
    test_mul();
    test_div_zero();
    test_halt();
    test_read_fkv_negative_operand();
    test_write_fkv_negative_operand();
    printf("vm tests passed\n");
    return 0;
}
