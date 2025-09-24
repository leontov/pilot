/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "vm/vm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} byte_buffer;

static int bb_push(byte_buffer *bb, uint8_t byte) {
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

static int emit_push_number(byte_buffer *bb, uint64_t value) {
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

static void scheduler_init_default(vm_scheduler_t *sched, uint32_t gas_quantum) {
    assert(vm_scheduler_init(sched, 4, 128, gas_quantum, 16) == 0);
}

static void run_single_program(vm_scheduler_t *sched,
                               byte_buffer *bb,
                               vm_limits_t limits,
                               uint32_t priority,
                               vm_result_t *result) {
    assert(bb_push(bb, 0x12) == 0); // HALT
    prog_t prog = {bb->data, bb->len};
    vm_context_t *ctx = NULL;
    assert(vm_scheduler_spawn(sched, &prog, &limits, priority, NULL, result, &ctx) == 0);
    while (!vm_context_finished(ctx)) {
        assert(vm_scheduler_step(sched) == 0);
    }
    vm_scheduler_release(sched, ctx);
}

static void test_random_deterministic(void) {
    vm_scheduler_t sched;
    scheduler_init_default(&sched, 16);
    vm_set_seed(42);

    byte_buffer bb = {0};
    assert(bb_push(&bb, 0x0F) == 0); // RANDOM10

    vm_result_t result = {0};
    vm_limits_t limits = {512, 128};
    run_single_program(&sched, &bb, limits, 1, &result);
    assert(result.status == VM_OK);
    assert(result.result == 1083814273ull);

    free(bb.data);
    vm_scheduler_destroy(&sched);
}

static void test_add(void) {
    vm_scheduler_t sched;
    scheduler_init_default(&sched, 32);

    byte_buffer bb = {0};
    assert(emit_push_number(&bb, 2) == 0);
    assert(emit_push_number(&bb, 2) == 0);
    assert(bb_push(&bb, 0x02) == 0); // ADD

    vm_result_t result = {0};
    vm_limits_t limits = {512, 128};
    run_single_program(&sched, &bb, limits, 1, &result);
    assert(result.status == VM_OK);
    assert(result.result == 4);

    free(bb.data);
    vm_scheduler_destroy(&sched);
}

static void test_mul(void) {
    vm_scheduler_t sched;
    scheduler_init_default(&sched, 32);

    byte_buffer bb = {0};
    assert(emit_push_number(&bb, 126) == 0);
    assert(emit_push_number(&bb, 6) == 0);
    assert(bb_push(&bb, 0x04) == 0); // MUL

    vm_result_t result = {0};
    vm_limits_t limits = {512, 128};
    run_single_program(&sched, &bb, limits, 1, &result);
    assert(result.status == VM_OK);
    assert(result.result == 756);

    free(bb.data);
    vm_scheduler_destroy(&sched);
}

static void test_div_zero(void) {
    vm_scheduler_t sched;
    scheduler_init_default(&sched, 16);

    byte_buffer bb = {0};
    assert(emit_push_number(&bb, 8) == 0);
    assert(emit_push_number(&bb, 0) == 0);
    assert(bb_push(&bb, 0x05) == 0); // DIV

    vm_result_t result = {0};
    vm_limits_t limits = {512, 128};
    run_single_program(&sched, &bb, limits, 1, &result);
    assert(result.status == VM_ERR_DIV_BY_ZERO);

    free(bb.data);
    vm_scheduler_destroy(&sched);
}

static void test_halt(void) {
    vm_scheduler_t sched;
    scheduler_init_default(&sched, 8);

    byte_buffer bb = {0};
    uint8_t program[] = {0x01, 0x05, 0x12, 0x01, 0x09};
    for (size_t i = 0; i < sizeof(program); ++i) {
        assert(bb_push(&bb, program[i]) == 0);
    }

    vm_result_t result = {0};
    vm_limits_t limits = {512, 128};
    run_single_program(&sched, &bb, limits, 1, &result);
    assert(result.status == VM_OK);
    assert(result.halted == 1);
    assert(result.steps == 2);
    assert(result.result == 5);

    free(bb.data);
    vm_scheduler_destroy(&sched);
}

static void test_scheduler_preemption(void) {
    vm_scheduler_t sched;
    scheduler_init_default(&sched, 1); // force preemption every instruction slice

    byte_buffer bb1 = {0};
    byte_buffer bb2 = {0};
    assert(emit_push_number(&bb1, 2) == 0);
    assert(emit_push_number(&bb1, 3) == 0);
    assert(bb_push(&bb1, 0x02) == 0); // ADD
    assert(bb_push(&bb1, 0x12) == 0); // HALT

    assert(emit_push_number(&bb2, 9) == 0);
    assert(emit_push_number(&bb2, 4) == 0);
    assert(bb_push(&bb2, 0x03) == 0); // SUB
    assert(bb_push(&bb2, 0x12) == 0); // HALT

    vm_limits_t limits = {256, 128};
    vm_result_t r1 = {0};
    vm_result_t r2 = {0};

    prog_t prog1 = {bb1.data, bb1.len};
    prog_t prog2 = {bb2.data, bb2.len};

    vm_context_t *ctx1 = NULL;
    vm_context_t *ctx2 = NULL;
    assert(vm_scheduler_spawn(&sched, &prog1, &limits, 2, NULL, &r1, &ctx1) == 0);
    assert(vm_scheduler_spawn(&sched, &prog2, &limits, 1, NULL, &r2, &ctx2) == 0);

    while (!vm_context_finished(ctx1) || !vm_context_finished(ctx2)) {
        assert(vm_scheduler_step(&sched) == 0);
    }

    assert(r1.status == VM_OK);
    assert(r1.result == 5);
    assert(r2.status == VM_OK);
    assert(r2.result == 5);
    assert(vm_scheduler_ready_count(&sched) == 0);

    vm_scheduler_release(&sched, ctx1);
    vm_scheduler_release(&sched, ctx2);
    free(bb1.data);
    free(bb2.data);
    vm_scheduler_destroy(&sched);
}

static void test_scheduler_priority_order(void) {
    vm_scheduler_t sched;
    scheduler_init_default(&sched, 1);

    uint8_t low_prog_bytes[] = {0x01, 0x02, 0x01, 0x03, 0x02, 0x12};
    uint8_t high_prog_bytes[] = {0x01, 0x04, 0x01, 0x05, 0x02, 0x12};

    prog_t low_prog = {low_prog_bytes, sizeof(low_prog_bytes)};
    prog_t high_prog = {high_prog_bytes, sizeof(high_prog_bytes)};

    vm_limits_t limits = {64, 32};
    vm_result_t low_res = {0};
    vm_result_t high_res = {0};

    vm_context_t *low_ctx = NULL;
    vm_context_t *high_ctx = NULL;

    assert(vm_scheduler_spawn(&sched, &low_prog, &limits, 1, NULL, &low_res, &low_ctx) == 0);
    assert(vm_scheduler_spawn(&sched, &high_prog, &limits, 9, NULL, &high_res, &high_ctx) == 0);

    assert(low_ctx->steps == 0);
    assert(high_ctx->steps == 0);

    assert(vm_scheduler_step(&sched) == 0);
    assert(high_ctx->steps == 1);
    assert(low_ctx->steps == 0);

    while (!vm_context_finished(high_ctx)) {
        size_t before = high_ctx->steps;
        assert(vm_scheduler_step(&sched) == 0);
        assert(high_ctx->steps > before);
        assert(low_ctx->steps == 0);
    }

    assert(high_res.status == VM_OK);
    assert(vm_context_finished(high_ctx));

    assert(vm_scheduler_step(&sched) == 0);
    assert(low_ctx->steps == 1);

    while (!vm_context_finished(low_ctx)) {
        size_t before = low_ctx->steps;
        assert(vm_scheduler_step(&sched) == 0);
        assert(low_ctx->steps > before);
    }

    assert(low_res.status == VM_OK);
    vm_scheduler_release(&sched, high_ctx);
    vm_scheduler_release(&sched, low_ctx);
    vm_scheduler_destroy(&sched);
}

static void test_scheduler_gas_limit(void) {
    vm_scheduler_t sched;
    scheduler_init_default(&sched, 4);

    byte_buffer bb = {0};
    // Push a constant for the first jump
    assert(bb_push(&bb, 0x01) == 0);
    assert(bb_push(&bb, 0x01) == 0);
    size_t loop_start = bb.len;
    assert(bb_push(&bb, 0x11) == 0); // NOP
    assert(bb_push(&bb, 0x01) == 0);
    assert(bb_push(&bb, 0x01) == 0); // push 1 for JNZ
    assert(bb_push(&bb, 0x09) == 0); // JNZ
    size_t offset_pos = bb.len;
    assert(bb_push(&bb, 0x00) == 0);
    assert(bb_push(&bb, 0x00) == 0);
    int16_t offset = (int16_t)loop_start - (int16_t)(offset_pos + 2);
    bb.data[offset_pos] = (uint8_t)(offset & 0xFF);
    bb.data[offset_pos + 1] = (uint8_t)((offset >> 8) & 0xFF);

    vm_limits_t limits = {8, 16};
    vm_result_t result = {0};
    prog_t prog = {bb.data, bb.len};
    vm_context_t *ctx = NULL;
    assert(vm_scheduler_spawn(&sched, &prog, &limits, 1, NULL, &result, &ctx) == 0);
    while (!vm_context_finished(ctx)) {
        assert(vm_scheduler_step(&sched) == 0);
    }
    assert(result.status == VM_ERR_GAS_EXHAUSTED);
    assert(result.steps == limits.max_steps);
    vm_scheduler_release(&sched, ctx);

    free(bb.data);
    vm_scheduler_destroy(&sched);
}

static void test_trace_multiple_tasks(void) {
    vm_scheduler_t sched;
    scheduler_init_default(&sched, 2);

    uint8_t prog_a_bytes[] = {0x01, 0x02, 0x01, 0x03, 0x02, 0x12};
    uint8_t prog_b_bytes[] = {0x01, 0x04, 0x01, 0x02, 0x04, 0x12};

    prog_t prog_a = {prog_a_bytes, sizeof(prog_a_bytes)};
    prog_t prog_b = {prog_b_bytes, sizeof(prog_b_bytes)};

    vm_trace_entry_t entries_a[16];
    vm_trace_entry_t entries_b[16];
    vm_trace_t trace_a = {entries_a, 16, 0, 0};
    vm_trace_t trace_b = {entries_b, 16, 0, 0};

    vm_result_t result_a = {0};
    vm_result_t result_b = {0};
    vm_limits_t limits = {64, 32};

    vm_context_t *ctx_a = NULL;
    vm_context_t *ctx_b = NULL;
    assert(vm_scheduler_spawn(&sched, &prog_a, &limits, 1, &trace_a, &result_a, &ctx_a) == 0);
    assert(vm_scheduler_spawn(&sched, &prog_b, &limits, 1, &trace_b, &result_b, &ctx_b) == 0);

    while (!vm_context_finished(ctx_a) || !vm_context_finished(ctx_b)) {
        assert(vm_scheduler_step(&sched) == 0);
    }

    assert(result_a.status == VM_OK);
    assert(result_b.status == VM_OK);
    assert(trace_a.count == result_a.steps);
    assert(trace_b.count == result_b.steps);
    assert(trace_a.count >= 4);
    assert(trace_b.count >= 4);
    assert(trace_a.entries[trace_a.count - 1].opcode == 0x12);
    assert(trace_b.entries[trace_b.count - 1].opcode == 0x12);
    assert(vm_scheduler_ready_count(&sched) == 0);

    vm_scheduler_release(&sched, ctx_a);
    vm_scheduler_release(&sched, ctx_b);
    vm_scheduler_destroy(&sched);
}

static void test_scheduler_fuzz_matches_vm_run(void) {
    vm_scheduler_t sched;
    scheduler_init_default(&sched, 4);
    srand(1234);

    for (int iter = 0; iter < 64; ++iter) {
        byte_buffer bb = {0};
        size_t pushes = 1 + (size_t)(rand() % 5);
        for (size_t i = 0; i < pushes; ++i) {
            uint8_t digit = (uint8_t)(rand() % 10);
            assert(bb_push(&bb, 0x01) == 0);
            assert(bb_push(&bb, digit) == 0);
        }
        for (size_t i = 1; i < pushes; ++i) {
            assert(bb_push(&bb, 0x02) == 0);
        }
        assert(bb_push(&bb, 0x12) == 0);

        vm_limits_t limits = {64, 32};
        vm_result_t sched_result = {0};
        prog_t prog = {bb.data, bb.len};
        vm_context_t *ctx = NULL;
        assert(vm_scheduler_spawn(&sched, &prog, &limits, iter % 3, NULL, &sched_result, &ctx) == 0);
        while (!vm_context_finished(ctx)) {
            assert(vm_scheduler_step(&sched) == 0);
        }
        vm_scheduler_release(&sched, ctx);

        vm_result_t direct = {0};
        assert(vm_run(&prog, &limits, NULL, &direct) == 0);
        assert(sched_result.status == direct.status);
        assert(sched_result.result == direct.result);

        free(bb.data);
    }

    vm_scheduler_destroy(&sched);
}

int main(void) {
    test_random_deterministic();
    test_add();
    test_mul();
    test_div_zero();
    test_halt();
    test_scheduler_preemption();
    test_scheduler_priority_order();
    test_scheduler_gas_limit();
    test_trace_multiple_tasks();
    test_scheduler_fuzz_matches_vm_run();
    printf("vm tests passed\n");
    return 0;
}
