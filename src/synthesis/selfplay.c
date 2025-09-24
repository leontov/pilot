/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "synthesis/selfplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int random_int(unsigned int *state, int min_value, int max_value) {
    if (!state || min_value > max_value) {
        return min_value;
    }
    unsigned int span = (unsigned int)(max_value - min_value + 1);
    if (span == 0) {
        span = 1;
    }
    unsigned int value = rand_r(state);
    return min_value + (int)(value % span);
}

static char pick_operator(unsigned int *state, uint32_t difficulty) {
    if (!state) {
        return '+';
    }
    unsigned int roll = rand_r(state);
    if (difficulty <= 1) {
        return '+';
    }
    if (difficulty == 2) {
        return (roll & 1U) ? '-' : '+';
    }
    switch (roll % 3U) {
    case 0:
        return '+';
    case 1:
        return '-';
    default:
        return '*';
    }
}

static double apply_operator(double lhs, double rhs, char op) {
    switch (op) {
    case '+':
        return lhs + rhs;
    case '-':
        return lhs - rhs;
    case '*':
        return lhs * rhs;
    default:
        return lhs + rhs;
    }
}

int kolibri_selfplay_generate_task(unsigned int *state,
                                   uint32_t max_difficulty,
                                   KolibriSelfplayTask *out_task) {
    if (!state || !out_task) {
        return -1;
    }

    memset(out_task, 0, sizeof(*out_task));

    uint32_t difficulty_cap = max_difficulty == 0 ? 1 : max_difficulty;
    unsigned int roll = rand_r(state);
    uint32_t difficulty = (uint32_t)(roll % difficulty_cap) + 1U;

    size_t operand_count = (difficulty >= 3U) ? 3U : 2U;
    if (operand_count > KOLIBRI_SELFPLAY_MAX_OPERANDS) {
        operand_count = KOLIBRI_SELFPLAY_MAX_OPERANDS;
    }

    for (size_t i = 0; i < operand_count; ++i) {
        int bound = 9;
        if (difficulty >= 3U) {
            bound = 12;
        } else if (difficulty == 2U) {
            bound = 20;
        }
        int min_value = (difficulty > 1U) ? -bound : 0;
        int max_value = bound;
        int value = random_int(state, min_value, max_value);
        if (difficulty <= 1U && value < 0) {
            value = -value;
        }
        out_task->operands[i] = (double)value;
    }

    out_task->operand_count = operand_count;
    out_task->operator_count = (operand_count > 1) ? operand_count - 1 : 0;

    for (size_t i = 0; i < out_task->operator_count; ++i) {
        out_task->operators[i] = pick_operator(state, difficulty);
    }

    double result = out_task->operands[0];
    for (size_t i = 0; i < out_task->operator_count; ++i) {
        result = apply_operator(result, out_task->operands[i + 1], out_task->operators[i]);
    }
    out_task->expected_result = result;
    out_task->difficulty = difficulty;

    int written = snprintf(out_task->description,
                           sizeof(out_task->description),
                           "Evaluate: %.0f",
                           out_task->operands[0]);
    size_t used = (written > 0) ? (size_t)written : 0U;
    for (size_t i = 0; i < out_task->operator_count && used < sizeof(out_task->description); ++i) {
        written = snprintf(out_task->description + used,
                           sizeof(out_task->description) - used,
                           " %c %.0f",
                           out_task->operators[i],
                           out_task->operands[i + 1]);
        if (written < 0) {
            out_task->description[used] = '\0';
            break;
        }
        used += (size_t)written;
    }

    return 0;
}
