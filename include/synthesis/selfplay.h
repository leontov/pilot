/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_SYNTHESIS_SELFPLAY_H
#define KOLIBRI_SYNTHESIS_SELFPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KOLIBRI_SELFPLAY_MAX_OPERANDS 3

typedef struct {
    char description[128];
    double operands[KOLIBRI_SELFPLAY_MAX_OPERANDS];
    char operators[KOLIBRI_SELFPLAY_MAX_OPERANDS - 1];
    size_t operand_count;
    size_t operator_count;
    double expected_result;
    uint32_t difficulty;
} KolibriSelfplayTask;

int kolibri_selfplay_generate_task(unsigned int *state,
                                   uint32_t max_difficulty,
                                   KolibriSelfplayTask *out_task);

#ifdef __cplusplus
}
#endif

#endif
