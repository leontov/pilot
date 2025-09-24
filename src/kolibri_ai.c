/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#define _POSIX_C_SOURCE 200809L

#include "kolibri_ai.h"

#include "util/log.h"
#include <math.h>
#include <pthread.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    KolibriAI *ai;
} kolibri_worker_ctx_t;

#define KOLIBRI_POE_THRESHOLD 0.6
#define KOLIBRI_MAX_PROGRAM_SIZE 256
#define KOLIBRI_SYNC_DATASET_LIMIT 16
#define KOLIBRI_PRIORITY_EPSILON 1e-9

typedef struct {
    char *data;
    size_t size;
} curl_buffer_t;

typedef enum {
    KOLIBRI_JOB_SELFPLAY = 0,
    KOLIBRI_JOB_SEARCH = 1
} kolibri_job_type_t;

typedef struct {
    uint8_t code[KOLIBRI_MAX_PROGRAM_SIZE];
    size_t length;
} kolibri_program_builder_t;

typedef struct {
    kolibri_job_type_t type;
    double priority;
    union {
        struct {
            KolibriSelfplayTask task;
        } selfplay;
        struct {
            Formula formula;
            double poe_hint;
        } search;
    } data;
} kolibri_job_t;

typedef struct {
    kolibri_job_t *items;
    size_t count;
    size_t capacity;
} kolibri_job_queue_t;

typedef struct {
    KolibriAISelfplayInteraction *entries;
    size_t count;
    size_t capacity;
} kolibri_interaction_log_t;

typedef struct {
    KolibriAI *ai;
    size_t limit;
    size_t produced;
} search_emit_context_t;

static size_t kolibri_curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    curl_buffer_t *buffer = userp;
    if (!buffer) {
        return 0;
    }
    char *new_data = realloc(buffer->data, buffer->size + total + 1);
    if (!new_data) {
        return 0;
    }
    memcpy(new_data + buffer->size, contents, total);
    buffer->size += total;
    new_data[buffer->size] = '\0';
    buffer->data = new_data;
    return total;
}

static double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

struct KolibriAI {
    FormulaCollection *library;
    KolibriCurriculumState curriculum;
    KolibriAIDataset dataset;
    KolibriMemoryModule memory;

    FormulaSearchConfig search_config;
    KolibriAISelfplayConfig selfplay_config;

    double average_reward;
    double recent_reward;
    double recent_poe;
    double recent_mdl;
    double planning_score;
    uint64_t iterations;

    uint64_t selfplay_total_interactions;
    unsigned int rng_state;

    char snapshot_path[256];
    uint32_t snapshot_limit;

    int running;
    pthread_t worker;
    pthread_mutex_t mutex;

    kolibri_job_queue_t job_queue;
    kolibri_interaction_log_t interaction_log;
};

static void copy_string_truncated(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t max_copy = dest_size - 1;
    size_t len = 0;
    while (len < max_copy && src[len] != '\0') {
        len++;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

static void json_escape_copy(const char *src, char *dest, size_t dest_size) {
    if (!dest || dest_size == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dest_size; ++i) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= dest_size) {
                break;
            }
            dest[j++] = '\\';
            dest[j++] = c;
        } else if ((unsigned char)c < 32) {
            dest[j++] = ' ';
        } else {
            dest[j++] = c;
        }
    }
    dest[j] = '\0';
}

static const char *json_find_array(const char *json,
                                   const char *key,
                                   const char **out_end) {
    if (!json || !key) {
        return NULL;
    }
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":[", key);
    const char *start = strstr(json, pattern);
    if (!start) {
        return NULL;
    }
    start += strlen(pattern);
    int depth = 1;
    const char *cursor = start;
    while (*cursor && depth > 0) {
        if (*cursor == '[') {
            depth++;
        } else if (*cursor == ']') {
            depth--;
            if (depth == 0) {
                if (out_end) {
                    *out_end = cursor;
                }
                return start;
            }
        }
        cursor++;
    }
    return NULL;
}

static void curriculum_init(KolibriCurriculumState *state) {
    if (!state) {
        return;
    }
    const double defaults[KOLIBRI_DIFFICULTY_COUNT] = {0.45, 0.30, 0.18, 0.07};
    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
        state->distribution[i] = defaults[i];
        state->success_ema[i] = 0.6;
        state->reward_ema[i] = 0.55;
        state->sample_count[i] = 0;
    }
    state->global_success_ema = 0.6;
    state->integral_error = 0.0;
    state->last_error = 0.0;
    state->temperature = 0.6;
    state->ema_alpha = 0.15;
    state->current_level = KOLIBRI_DIFFICULTY_FOUNDATION;
}

static size_t curriculum_index_for_difficulty(uint32_t difficulty) {
    if (difficulty <= 1U) {
        return (size_t)KOLIBRI_DIFFICULTY_FOUNDATION;
    }
    if (difficulty == 2U) {
        return (size_t)KOLIBRI_DIFFICULTY_SKILLS;
    }
    if (difficulty == 3U) {
        return (size_t)KOLIBRI_DIFFICULTY_ADVANCED;
    }
    return (size_t)KOLIBRI_DIFFICULTY_CHALLENGE;
}

static void curriculum_normalize_distribution(KolibriCurriculumState *state) {
    double sum = 0.0;
    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
        if (state->distribution[i] < 0.0) {
            state->distribution[i] = 0.0;
        }
        sum += state->distribution[i];
    }
    if (sum <= 0.0) {
        double uniform = 1.0 / (double)KOLIBRI_DIFFICULTY_COUNT;
        for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
            state->distribution[i] = uniform;
        }
        return;
    }
    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
        state->distribution[i] /= sum;
    }
}

static void curriculum_rebalance(KolibriCurriculumState *state) {
    if (!state) {
        return;
    }
    double weights[KOLIBRI_DIFFICULTY_COUNT];
    double weight_sum = 0.0;
    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
        double scarcity = clamp01(1.0 - state->success_ema[i]);
        double reward = clamp01(state->reward_ema[i]);
        double exploration = 1.0 / (1.0 + (double)state->sample_count[i]);
        double temperature = clamp01(state->temperature);
        double difficulty_bias = 1.0 + 0.15 * (double)i * temperature;
        double weight = 0.55 * scarcity + 0.30 * reward + 0.15 * exploration;
        weight *= difficulty_bias;
        if (weight < 0.0001) {
            weight = 0.0001;
        }
        weights[i] = weight;
        weight_sum += weight;
    }
    if (weight_sum <= 0.0) {
        weight_sum = (double)KOLIBRI_DIFFICULTY_COUNT;
        for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
            weights[i] = 1.0;
        }
    }
    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
        double target = weights[i] / weight_sum;
        state->distribution[i] =
            (1.0 - state->ema_alpha) * state->distribution[i] + state->ema_alpha * target;
    }
    curriculum_normalize_distribution(state);
}

static void curriculum_register_result(KolibriCurriculumState *state,
                                       uint32_t difficulty,
                                       double reward,
                                       int success) {
    if (!state) {
        return;
    }
    size_t index = curriculum_index_for_difficulty(difficulty);
    double alpha = state->ema_alpha > 0.0 ? state->ema_alpha : 0.1;
    double outcome = success ? 1.0 : clamp01(reward);
    state->success_ema[index] = (1.0 - alpha) * state->success_ema[index] + alpha * outcome;
    state->reward_ema[index] = (1.0 - alpha) * state->reward_ema[index] + alpha * clamp01(reward);
    state->sample_count[index] += 1ULL;
    double target_success = 0.75;
    double error = target_success - state->success_ema[index];
    state->integral_error = 0.92 * state->integral_error + error;
    state->last_error = error;
    state->global_success_ema =
        (1.0 - alpha) * state->global_success_ema + alpha * outcome;
    double temperature = state->temperature + 0.08 * error + 0.01 * state->integral_error;
    if (temperature < 0.1) {
        temperature = 0.1;
    } else if (temperature > 1.5) {
        temperature = 1.5;
    }
    state->temperature = temperature;
    curriculum_rebalance(state);
}

static KolibriDifficultyLevel curriculum_pick_level(const KolibriCurriculumState *state,
                                                    double sample) {
    if (!state) {
        return KOLIBRI_DIFFICULTY_FOUNDATION;
    }
    double cumulative = 0.0;
    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
        cumulative += state->distribution[i];
        if (sample <= cumulative + 1e-9) {
            return (KolibriDifficultyLevel)i;
        }
    }
    return KOLIBRI_DIFFICULTY_CHALLENGE;
}

static void dataset_init(KolibriAIDataset *dataset) {
    if (!dataset) {
        return;
    }
    dataset->entries = NULL;
    dataset->count = 0;
    dataset->capacity = 0;
}

static void dataset_clear(KolibriAIDataset *dataset) {
    if (!dataset) {
        return;
    }
    free(dataset->entries);
    dataset->entries = NULL;
    dataset->count = 0;
    dataset->capacity = 0;
}

static int dataset_reserve(KolibriAIDataset *dataset, size_t needed) {
    if (!dataset) {
        return -1;
    }
    if (dataset->capacity >= needed) {
        return 0;
    }
    size_t new_capacity = dataset->capacity == 0 ? 8 : dataset->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    KolibriAIDatasetEntry *entries =
        realloc(dataset->entries, new_capacity * sizeof(*entries));
    if (!entries) {
        return -1;
    }
    dataset->entries = entries;
    dataset->capacity = new_capacity;
    return 0;
}

static int dataset_append(KolibriAIDataset *dataset,
                          const KolibriAIDatasetEntry *entry) {
    if (!dataset || !entry) {
        return -1;
    }
    if (dataset_reserve(dataset, dataset->count + 1) != 0) {
        return -1;
    }
    dataset->entries[dataset->count++] = *entry;
    return 0;
}

static double dataset_entry_score(const KolibriAIDatasetEntry *entry, time_t reference) {
    if (!entry) {
        return 0.0;
    }
    double reward = clamp01(entry->reward);
    double poe = clamp01(entry->poe);
    double mdl_penalty = clamp01(entry->mdl);
    double recency = 1.0;
    if (reference > entry->timestamp) {
        double age_seconds = difftime(reference, entry->timestamp);
        if (age_seconds > 0.0) {
            recency = 1.0 / (1.0 + age_seconds / 120.0);
        }
    }
    double score = 0.45 * reward + 0.35 * poe + 0.20 * clamp01(recency);
    score -= 0.15 * mdl_penalty;
    return score;
}

static void dataset_trim(KolibriAIDataset *dataset, size_t limit) {
    if (!dataset) {
        return;
    }
    if (limit == 0) {
        dataset->count = 0;
        return;
    }
    if (dataset->count <= limit) {
        return;
    }
    time_t reference = time(NULL);
    while (dataset->count > limit) {
        size_t worst_index = 0;
        double worst_score = dataset_entry_score(&dataset->entries[0], reference);
        for (size_t i = 1; i < dataset->count; ++i) {
            double score = dataset_entry_score(&dataset->entries[i], reference);
            if (score < worst_score) {
                worst_score = score;
                worst_index = i;
            }
        }
        if (worst_index < dataset->count - 1) {
            memmove(dataset->entries + worst_index,
                    dataset->entries + worst_index + 1,
                    (dataset->count - worst_index - 1) * sizeof(dataset->entries[0]));
        }
        dataset->count--;
    }
}

static void memory_init(KolibriMemoryModule *memory) {
    if (!memory) {
        return;
    }
    memory->facts = NULL;
    memory->count = 0;
    memory->capacity = 0;
}

static void memory_clear(KolibriMemoryModule *memory) {
    if (!memory) {
        return;
    }
    free(memory->facts);
    memory->facts = NULL;
    memory->count = 0;
    memory->capacity = 0;
}

static int memory_reserve(KolibriMemoryModule *memory, size_t needed) {
    if (!memory) {
        return -1;
    }
    if (memory->capacity >= needed) {
        return 0;
    }
    size_t new_capacity = memory->capacity == 0 ? 8 : memory->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    KolibriMemoryFact *facts =
        realloc(memory->facts, new_capacity * sizeof(*facts));
    if (!facts) {
        return -1;
    }
    memory->facts = facts;
    memory->capacity = new_capacity;
    return 0;
}

static void memory_record(KolibriMemoryModule *memory,
                          const KolibriMemoryFact *fact,
                          size_t limit) {
    if (!memory || !fact) {
        return;
    }
    if (memory_reserve(memory, memory->count + 1) != 0) {
        return;
    }
    memory->facts[memory->count++] = *fact;
    if (limit > 0 && memory->count > limit) {
        size_t offset = memory->count - limit;
        memmove(memory->facts,
                memory->facts + offset,
                limit * sizeof(memory->facts[0]));
        memory->count = limit;
    }
}

static void job_queue_init(kolibri_job_queue_t *queue) {
    if (!queue) {
        return;
    }
    queue->items = NULL;
    queue->count = 0;
    queue->capacity = 0;
}

static void job_queue_clear(kolibri_job_queue_t *queue) {
    if (!queue) {
        return;
    }
    free(queue->items);
    queue->items = NULL;
    queue->count = 0;
    queue->capacity = 0;
}

static int job_queue_reserve(kolibri_job_queue_t *queue, size_t needed) {
    if (!queue) {
        return -1;
    }
    if (queue->capacity >= needed) {
        return 0;
    }
    size_t new_capacity = queue->capacity == 0 ? 8 : queue->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    kolibri_job_t *items = realloc(queue->items, new_capacity * sizeof(*items));
    if (!items) {
        return -1;
    }
    queue->items = items;
    queue->capacity = new_capacity;
    return 0;
}

static int job_queue_push(kolibri_job_queue_t *queue, const kolibri_job_t *job) {
    if (!queue || !job) {
        return -1;
    }
    if (job_queue_reserve(queue, queue->count + 1) != 0) {
        return -1;
    }
    size_t index = queue->count;
    while (index > 0) {
        double prev_priority = queue->items[index - 1].priority;
        if (prev_priority + KOLIBRI_PRIORITY_EPSILON >= job->priority) {
            break;
        }
        queue->items[index] = queue->items[index - 1];
        index--;
    }
    queue->items[index] = *job;
    queue->count++;
    return 0;
}

static int job_queue_pop(kolibri_job_queue_t *queue, kolibri_job_t *out_job) {
    if (!queue || queue->count == 0 || !out_job) {
        return -1;
    }
    *out_job = queue->items[0];
    if (queue->count > 1) {
        memmove(queue->items,
                queue->items + 1,
                (queue->count - 1) * sizeof(queue->items[0]));
    }
    queue->count--;
    return 0;
}

static void interaction_log_init(kolibri_interaction_log_t *log) {
    if (!log) {
        return;
    }
    log->entries = NULL;
    log->count = 0;
    log->capacity = 0;
}

static void interaction_log_clear(kolibri_interaction_log_t *log) {
    if (!log) {
        return;
    }
    free(log->entries);
    log->entries = NULL;
    log->count = 0;
    log->capacity = 0;
}

static int interaction_log_reserve(kolibri_interaction_log_t *log, size_t needed) {
    if (!log) {
        return -1;
    }
    if (log->capacity >= needed) {
        return 0;
    }
    size_t new_capacity = log->capacity == 0 ? 16 : log->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    KolibriAISelfplayInteraction *entries =
        realloc(log->entries, new_capacity * sizeof(*entries));
    if (!entries) {
        return -1;
    }
    log->entries = entries;
    log->capacity = new_capacity;
    return 0;
}

static void interaction_log_append(kolibri_interaction_log_t *log,
                                   const KolibriAISelfplayInteraction *interaction,
                                   size_t limit) {
    if (!log || !interaction) {
        return;
    }
    if (interaction_log_reserve(log, log->count + 1) != 0) {
        return;
    }
    log->entries[log->count++] = *interaction;
    if (limit > 0 && log->count > limit) {
        size_t offset = log->count - limit;
        memmove(log->entries,
                log->entries + offset,
                limit * sizeof(log->entries[0]));
        log->count = limit;
    }
}

static void program_builder_init(kolibri_program_builder_t *builder) {
    if (!builder) {
        return;
    }
    builder->length = 0;
}

static int program_emit(kolibri_program_builder_t *builder, uint8_t byte) {
    if (!builder || builder->length >= KOLIBRI_MAX_PROGRAM_SIZE) {
        return -1;
    }
    builder->code[builder->length++] = byte;
    return 0;
}

static int program_push_digit(kolibri_program_builder_t *builder, uint8_t digit) {
    if (!builder || digit > 9) {
        return -1;
    }
    if (program_emit(builder, 0x01) != 0) {
        return -1;
    }
    if (program_emit(builder, digit) != 0) {
        return -1;
    }
    return 0;
}

static int program_push_integer(kolibri_program_builder_t *builder, int value) {
    if (!builder) {
        return -1;
    }
    int negative = value < 0;
    unsigned int abs_value = (unsigned int)(negative ? -value : value);
    if (negative) {
        if (program_push_digit(builder, 0) != 0) {
            return -1;
        }
    }
    if (abs_value == 0) {
        if (program_push_digit(builder, 0) != 0) {
            return -1;
        }
    } else {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%u", abs_value);
        if (program_push_digit(builder, (uint8_t)(buffer[0] - '0')) != 0) {
            return -1;
        }
        for (size_t i = 1; buffer[i] != '\0'; ++i) {
            if (program_push_digit(builder, 9) != 0) {
                return -1;
            }
            if (program_push_digit(builder, 1) != 0) {
                return -1;
            }
            if (program_emit(builder, 0x02) != 0) { // ADD10 => 9 + 1
                return -1;
            }
            if (program_emit(builder, 0x04) != 0) { // MUL10
                return -1;
            }
            if (program_push_digit(builder, (uint8_t)(buffer[i] - '0')) != 0) {
                return -1;
            }
            if (program_emit(builder, 0x02) != 0) { // ADD10
                return -1;
            }
        }
    }
    if (negative) {
        if (program_emit(builder, 0x03) != 0) { // SUB10 => 0 - value
            return -1;
        }
    }
    return 0;
}

static int compile_selfplay_program(const KolibriSelfplayTask *task,
                                    uint8_t **out_code,
                                    size_t *out_len) {
    if (!task || !out_code || !out_len) {
        return -1;
    }
    kolibri_program_builder_t builder;
    program_builder_init(&builder);

    int first_operand = (int)lround(task->operands[0]);
    if (program_push_integer(&builder, first_operand) != 0) {
        return -1;
    }

    for (size_t i = 0; i < task->operator_count; ++i) {
        int operand = (int)lround(task->operands[i + 1]);
        if (program_push_integer(&builder, operand) != 0) {
            return -1;
        }
        char op = task->operators[i];
        uint8_t opcode = 0x02; // default ADD10
        if (op == '+') {
            opcode = 0x02;
        } else if (op == '-') {
            opcode = 0x03;
        } else if (op == '*') {
            opcode = 0x04;
        }
        if (program_emit(&builder, opcode) != 0) {
            return -1;
        }
    }

    if (program_emit(&builder, 0x12) != 0) { // HALT
        return -1;
    }

    uint8_t *code = malloc(builder.length);
    if (!code) {
        return -1;
    }
    memcpy(code, builder.code, builder.length);
    *out_code = code;
    *out_len = builder.length;
    return 0;
}

static double compute_selfplay_reward(const KolibriSelfplayTask *task, double predicted) {
    if (!task) {
        return 0.0;
    }
    double expected = task->expected_result;
    double denom = fabs(expected);
    if (denom < 1.0) {
        denom = 1.0;
    }
    double error = fabs(predicted - expected) / denom;
    if (error > 1.0) {
        error = 1.0;
    }
    return fmax(0.0, 1.0 - error);
}

static FormulaMemorySnapshot kolibri_memory_snapshot_build(const KolibriMemoryModule *memory) {
    FormulaMemorySnapshot snapshot = {0};
    if (!memory || memory->count == 0) {
        return snapshot;
    }
    FormulaMemoryFact *facts = calloc(memory->count, sizeof(*facts));
    if (!facts) {
        return snapshot;
    }
    for (size_t i = 0; i < memory->count; ++i) {
        const KolibriMemoryFact *src = &memory->facts[i];
        FormulaMemoryFact *dst = &facts[i];
        copy_string_truncated(dst->fact_id, sizeof(dst->fact_id), src->key);
        copy_string_truncated(dst->description, sizeof(dst->description), src->value);
        dst->importance = src->salience;
        dst->reward = src->salience;
        dst->timestamp = src->last_updated;
    }
    snapshot.facts = facts;
    snapshot.count = memory->count;
    return snapshot;
}

static void kolibri_memory_snapshot_release(FormulaMemorySnapshot *snapshot) {
    if (!snapshot) {
        return;
    }
    free(snapshot->facts);
    snapshot->facts = NULL;
    snapshot->count = 0;
}

static double compute_selfplay_priority(const KolibriAI *ai,
                                        const KolibriSelfplayTask *task,
                                        KolibriDifficultyLevel target_level) {
    if (!ai || !task) {
        return 0.0;
    }
    const KolibriCurriculumState *curriculum = &ai->curriculum;
    size_t index = curriculum_index_for_difficulty(task->difficulty);
    double scarcity = clamp01(1.0 - curriculum->success_ema[index]);
    double exploration = 1.0 / (1.0 + (double)curriculum->sample_count[index]);
    double alignment = clamp01(curriculum->reward_ema[index]);
    double target_bonus = (index == (size_t)target_level) ? 0.18 : 0.0;
    double difficulty_norm = 0.0;
    if (ai->selfplay_config.max_difficulty > 0) {
        difficulty_norm = clamp01((double)task->difficulty /
                                  (double)ai->selfplay_config.max_difficulty);
    }
    double temperature = clamp01(curriculum->temperature);
    double difficulty_bias = 0.1 * ((double)index / (double)(KOLIBRI_DIFFICULTY_COUNT - 1));
    double base = 0.48 * scarcity + 0.22 * exploration + 0.20 * alignment +
                  0.10 * (difficulty_norm + difficulty_bias * temperature);
    base += target_bonus;
    return clamp01(base);
}

static double compute_search_priority(const KolibriAI *ai,
                                      const Formula *formula,
                                      double poe_hint,
                                      size_t produced,
                                      size_t limit) {
    if (!ai || !formula) {
        return 0.0;
    }
    double novelty = 1.0;
    if (limit > 0 && produced < limit) {
        novelty = 1.0 - ((double)produced / (double)limit);
    }
    double maturity = clamp01((double)formula->confirmations / 4.0);
    double delta = poe_hint - ai->average_reward;
    if (delta < 0.0) {
        delta *= 0.6;
    }
    double base = clamp01(poe_hint);
    double priority = 0.55 * base + 0.20 * clamp01(novelty) +
                      0.15 * clamp01(delta + 0.5) + 0.10 * maturity;
    return clamp01(priority);
}

static int scheduler_emit_formula_cb(const Formula *formula, void *user_data) {
    search_emit_context_t *ctx = user_data;
    if (!ctx || !ctx->ai || !formula) {
        return 1;
    }
    if (ctx->limit > 0 && ctx->produced >= ctx->limit) {
        return 1;
    }
    kolibri_job_t job;
    memset(&job, 0, sizeof(job));
    job.type = KOLIBRI_JOB_SEARCH;
    job.data.search.formula = *formula;
    job.data.search.poe_hint = formula->effectiveness;
    job.priority = compute_search_priority(ctx->ai,
                                           formula,
                                           job.data.search.poe_hint,
                                           ctx->produced,
                                           ctx->limit);
    if (job_queue_push(&ctx->ai->job_queue, &job) != 0) {
        return 1;
    }
    ctx->produced++;
    if (ctx->limit > 0 && ctx->produced >= ctx->limit) {
        return 1;
    }
    return 0;
}

static void scheduler_enqueue_locked(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    size_t target = ai->selfplay_config.tasks_per_iteration +
                    ai->search_config.max_candidates;
    if (target == 0) {
        target = 8;
    }
    size_t missing = ai->job_queue.count >= target ? 0 : target - ai->job_queue.count;

    size_t selfplay_to_add = ai->selfplay_config.tasks_per_iteration;
    if (selfplay_to_add > missing) {
        selfplay_to_add = missing;
    }

    for (size_t i = 0; i < selfplay_to_add; ++i) {
        kolibri_job_t job;
        memset(&job, 0, sizeof(job));
        job.type = KOLIBRI_JOB_SELFPLAY;

        double best_priority = -1.0;
        KolibriSelfplayTask best_task;
        memset(&best_task, 0, sizeof(best_task));

        const int attempt_cap = 4;
        for (int attempt = 0; attempt < attempt_cap; ++attempt) {
            KolibriSelfplayTask candidate;
            memset(&candidate, 0, sizeof(candidate));
            if (kolibri_selfplay_generate_task(&ai->rng_state,
                                               ai->selfplay_config.max_difficulty,
                                               &candidate) != 0) {
                break;
            }
            double priority = compute_selfplay_priority(
                ai, &candidate, ai->curriculum.current_level);
            if (priority > best_priority + KOLIBRI_PRIORITY_EPSILON) {
                best_priority = priority;
                best_task = candidate;
            }
            if (curriculum_index_for_difficulty(candidate.difficulty) ==
                (size_t)ai->curriculum.current_level && priority > 0.75) {
                break;
            }
        }

        if (best_priority < 0.0) {
            break;
        }
        job.data.selfplay.task = best_task;
        job.priority = best_priority;
        if (job_queue_push(&ai->job_queue, &job) != 0) {
            break;
        }
    }

    if (ai->job_queue.count >= target) {
        return;
    }

    size_t search_limit = target - ai->job_queue.count;
    if (search_limit > ai->search_config.max_candidates) {
        search_limit = ai->search_config.max_candidates;
    }
    if (search_limit == 0) {
        return;
    }

    FormulaMemorySnapshot snapshot =
        kolibri_memory_snapshot_build(&ai->memory);
    const FormulaMemorySnapshot *snapshot_ptr = snapshot.count > 0 ? &snapshot : NULL;
    search_emit_context_t ctx = {ai, search_limit, 0};
    formula_search_enumerate(ai->library,
                             snapshot_ptr,
                             &ai->search_config,
                             scheduler_emit_formula_cb,
                             &ctx);
    kolibri_memory_snapshot_release(&snapshot);
}

static void process_selfplay_job(KolibriAI *ai, const KolibriSelfplayTask *task) {
    if (!ai || !task) {
        return;
    }
    uint8_t *code = NULL;
    size_t code_len = 0;
    if (compile_selfplay_program(task, &code, &code_len) != 0) {
        return;
    }
    prog_t program = {code, code_len};
    vm_limits_t limits = {256, 64};
    vm_result_t result = {0};
    if (vm_run(&program, &limits, NULL, &result) != 0) {
        free(code);
        return;
    }
    double predicted = (double)result.result;
    double reward = compute_selfplay_reward(task, predicted);
    KolibriAISelfplayInteraction interaction;
    memset(&interaction, 0, sizeof(interaction));
    interaction.task = *task;
    interaction.predicted_result = predicted;
    interaction.error = predicted - task->expected_result;
    interaction.reward = reward;
    interaction.success = fabs(interaction.error) < 1e-6;
    kolibri_ai_record_interaction(ai, &interaction);

    if (reward >= KOLIBRI_POE_THRESHOLD) {
        KolibriMemoryFact fact;
        memset(&fact, 0, sizeof(fact));
        snprintf(fact.key, sizeof(fact.key), "selfplay.%llu", (unsigned long long)ai->selfplay_total_interactions);
        copy_string_truncated(fact.value, sizeof(fact.value), task->description);
        fact.salience = reward;
        fact.last_updated = time(NULL);
        pthread_mutex_lock(&ai->mutex);
        memory_record(&ai->memory, &fact, ai->snapshot_limit);
        pthread_mutex_unlock(&ai->mutex);
    }

    free(code);
}

static void process_search_job(KolibriAI *ai,
                               const Formula *formula,
                               double poe_hint) {
    if (!ai || !formula) {
        return;
    }
    vm_result_t result = {0};
    double poe = 0.0;
    double mdl = 0.0;
    int rc = evaluate_formula_with_vm(formula, &result, &poe, &mdl, NULL);
    if (rc != 0) {
        poe = poe_hint;
        mdl = 1.0;
    }
    if (poe < KOLIBRI_POE_THRESHOLD && poe_hint < KOLIBRI_POE_THRESHOLD) {
        return;
    }

    Formula candidate = *formula;
    candidate.effectiveness = poe > 0.0 ? poe : poe_hint;
    FormulaExperience experience;
    memset(&experience, 0, sizeof(experience));
    experience.reward = candidate.effectiveness;
    experience.poe = candidate.effectiveness;
    experience.mdl = mdl;
    experience.imitation_score = 0.5 * candidate.effectiveness;
    experience.accuracy = 0.5 * candidate.effectiveness;
    snprintf(experience.source, sizeof(experience.source), "search");
    copy_string_truncated(experience.task_id, sizeof(experience.task_id), candidate.id);
    kolibri_ai_apply_reinforcement(ai, &candidate, &experience);
}

static unsigned int prng_next(KolibriAI *ai) {
    ai->rng_state = ai->rng_state * 1664525u + 1013904223u;
    return ai->rng_state;
}

static double rand_uniform(KolibriAI *ai) {
    return (double)(prng_next(ai) & 0xFFFFFF) / (double)0x1000000;
}

static void seed_library(KolibriAI *ai) {
    if (!ai || !ai->library) {
        return;
    }
    Formula base = {0};
    base.representation = FORMULA_REPRESENTATION_TEXT;
    snprintf(base.id, sizeof(base.id), "kolibri.seed.1");
    snprintf(base.content, sizeof(base.content), "f(x)=x+1");
    base.effectiveness = 0.5;
    base.created_at = time(NULL);
    base.tests_passed = 1;
    base.confirmations = 1;
    formula_collection_add(ai->library, &base);
}

static void update_average_reward(KolibriAI *ai) {
    if (!ai || !ai->library || ai->library->count == 0) {
        ai->average_reward = 0.0;
        return;
    }
    double total = 0.0;
    for (size_t i = 0; i < ai->library->count; ++i) {
        total += ai->library->formulas[i].effectiveness;
    }
    ai->average_reward = total / (double)ai->library->count;
}

static void *worker_thread(void *arg) {
    kolibri_worker_ctx_t *ctx = arg;
    KolibriAI *ai = ctx->ai;
    free(ctx);
    while (1) {
        pthread_mutex_lock(&ai->mutex);
        int running = ai->running;
        pthread_mutex_unlock(&ai->mutex);
        if (!running) {
            break;
        }
        kolibri_ai_process_iteration(ai);
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 50 * 1000 * 1000;
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static void configure_defaults(KolibriAI *ai, const kolibri_config_t *cfg) {
    if (!ai) {
        return;
    }
    ai->search_config = cfg ? cfg->search : formula_search_config_default();
    if (ai->search_config.max_candidates == 0) {
        ai->search_config = formula_search_config_default();
    }

    if (cfg) {
        ai->selfplay_config = cfg->selfplay;
        copy_string_truncated(ai->snapshot_path,
                              sizeof(ai->snapshot_path),
                              cfg->ai.snapshot_path);
        ai->snapshot_limit = cfg->ai.snapshot_limit;
        ai->rng_state = cfg->seed != 0 ? cfg->seed : (unsigned)time(NULL);
    } else {
        ai->selfplay_config.tasks_per_iteration = 8;
        ai->selfplay_config.max_difficulty = 4;
        copy_string_truncated(ai->snapshot_path,
                              sizeof(ai->snapshot_path),
                              "data/kolibri_ai_snapshot.json");
        ai->snapshot_limit = 1024;
        ai->rng_state = (unsigned)time(NULL);
    }
}

KolibriAI *kolibri_ai_create(const kolibri_config_t *cfg) {
    KolibriAI *ai = calloc(1, sizeof(*ai));
    if (!ai) {
        return NULL;
    }

    if (pthread_mutex_init(&ai->mutex, NULL) != 0) {
        free(ai);
        return NULL;
    }

    ai->library = formula_collection_create(8);
    if (!ai->library) {
        pthread_mutex_destroy(&ai->mutex);
        free(ai);
        return NULL;
    }

    curriculum_init(&ai->curriculum);
    dataset_init(&ai->dataset);
    memory_init(&ai->memory);
    job_queue_init(&ai->job_queue);
    interaction_log_init(&ai->interaction_log);
    configure_defaults(ai, cfg);
    seed_library(ai);
    update_average_reward(ai);
    return ai;
}

void kolibri_ai_destroy(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    kolibri_ai_stop(ai);
    dataset_clear(&ai->dataset);
    memory_clear(&ai->memory);
    job_queue_clear(&ai->job_queue);
    interaction_log_clear(&ai->interaction_log);
    if (ai->library) {
        formula_collection_destroy(ai->library);
    }
    pthread_mutex_destroy(&ai->mutex);
    free(ai);
}

void kolibri_ai_start(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    if (ai->running) {
        pthread_mutex_unlock(&ai->mutex);
        return;
    }
    ai->running = 1;
    pthread_mutex_unlock(&ai->mutex);

    kolibri_worker_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        pthread_mutex_lock(&ai->mutex);
        ai->running = 0;
        pthread_mutex_unlock(&ai->mutex);
        return;
    }
    ctx->ai = ai;
    if (pthread_create(&ai->worker, NULL, worker_thread, ctx) != 0) {
        free(ctx);
        pthread_mutex_lock(&ai->mutex);
        ai->running = 0;
        pthread_mutex_unlock(&ai->mutex);
    }
}

void kolibri_ai_stop(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    int was_running = ai->running;
    ai->running = 0;
    pthread_mutex_unlock(&ai->mutex);
    if (was_running) {
        pthread_join(ai->worker, NULL);
    }
}

void kolibri_ai_process_iteration(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    size_t max_jobs = 0;
    pthread_mutex_lock(&ai->mutex);
    ai->iterations++;
    double target = ai->average_reward > 0.0 ? ai->average_reward : 0.5;
    ai->planning_score = 0.9 * ai->planning_score + 0.1 * target;
    double sample = rand_uniform(ai);
    ai->curriculum.current_level = curriculum_pick_level(&ai->curriculum, sample);
    scheduler_enqueue_locked(ai);
    max_jobs = ai->selfplay_config.tasks_per_iteration +
               ai->search_config.max_candidates;
    if (max_jobs == 0) {
        max_jobs = 8;
    }
    pthread_mutex_unlock(&ai->mutex);
    size_t processed = 0;
    while (processed < max_jobs) {
        kolibri_job_t job;
        int has_job = 0;
        pthread_mutex_lock(&ai->mutex);
        has_job = job_queue_pop(&ai->job_queue, &job);
        pthread_mutex_unlock(&ai->mutex);
        if (has_job != 0) {
            break;
        }
        if (job.type == KOLIBRI_JOB_SELFPLAY) {
            process_selfplay_job(ai, &job.data.selfplay.task);
        } else if (job.type == KOLIBRI_JOB_SEARCH) {
            process_search_job(ai,
                               &job.data.search.formula,
                               job.data.search.poe_hint);
        }
        processed++;
    }
}

void kolibri_ai_set_selfplay_config(KolibriAI *ai,
                                    const KolibriAISelfplayConfig *config) {
    if (!ai || !config) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    ai->selfplay_config = *config;
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_record_interaction(KolibriAI *ai,
                                   const KolibriAISelfplayInteraction *interaction) {
    if (!ai || !interaction) {
        return;
    }
    KolibriAIDatasetEntry entry = {0};
    copy_string_truncated(entry.prompt,
                          sizeof(entry.prompt),
                          interaction->task.description);
    snprintf(entry.response, sizeof(entry.response), "%.3f", interaction->predicted_result);
    entry.reward = interaction->reward;
    entry.poe = interaction->reward;
    entry.mdl = fabs(interaction->error);
    entry.timestamp = time(NULL);

    pthread_mutex_lock(&ai->mutex);
    dataset_append(&ai->dataset, &entry);
    dataset_trim(&ai->dataset, ai->snapshot_limit);
    ai->selfplay_total_interactions++;
    ai->recent_reward = interaction->reward;
    ai->recent_poe = interaction->reward;
    ai->recent_mdl = entry.mdl;
    curriculum_register_result(&ai->curriculum,
                               interaction->task.difficulty,
                               interaction->reward,
                               interaction->success);
    interaction_log_append(&ai->interaction_log, interaction, ai->snapshot_limit);
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_apply_config(KolibriAI *ai, const kolibri_config_t *cfg) {
    if (!ai || !cfg) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    ai->search_config = cfg->search;
    ai->selfplay_config = cfg->selfplay;
    copy_string_truncated(ai->snapshot_path,
                          sizeof(ai->snapshot_path),
                          cfg->ai.snapshot_path);
    ai->snapshot_limit = cfg->ai.snapshot_limit;
    if (cfg->seed != 0) {
        ai->rng_state = cfg->seed;
    }
    dataset_trim(&ai->dataset, ai->snapshot_limit);
    if (ai->memory.count > ai->snapshot_limit) {
        size_t offset = ai->memory.count - ai->snapshot_limit;
        memmove(ai->memory.facts,
                ai->memory.facts + offset,
                ai->snapshot_limit * sizeof(ai->memory.facts[0]));
        ai->memory.count = ai->snapshot_limit;
    }
    if (ai->interaction_log.count > ai->snapshot_limit) {
        size_t offset = ai->interaction_log.count - ai->snapshot_limit;
        memmove(ai->interaction_log.entries,
                ai->interaction_log.entries + offset,
                ai->snapshot_limit * sizeof(ai->interaction_log.entries[0]));
        ai->interaction_log.count = ai->snapshot_limit;
    }
    pthread_mutex_unlock(&ai->mutex);
}

KolibriDifficultyLevel kolibri_ai_plan_actions(KolibriAI *ai,
                                               double *expected_reward) {
    if (!ai) {
        if (expected_reward) {
            *expected_reward = 0.0;
        }
        return KOLIBRI_DIFFICULTY_FOUNDATION;
    }
    pthread_mutex_lock(&ai->mutex);
    double sample = rand_uniform(ai);
    KolibriDifficultyLevel level = curriculum_pick_level(&ai->curriculum, sample);
    if (expected_reward) {
        *expected_reward = ai->average_reward;
    }
    pthread_mutex_unlock(&ai->mutex);
    return level;
}

int kolibri_ai_apply_reinforcement(KolibriAI *ai,
                                   const Formula *formula,
                                   const FormulaExperience *experience) {
    if (!ai || !formula || !experience) {
        return -1;
    }
    pthread_mutex_lock(&ai->mutex);
    ai->recent_reward = experience->reward;
    ai->recent_poe = experience->poe;
    ai->recent_mdl = experience->mdl;
    double planning = experience->poe - 0.25 * experience->mdl;
    if (planning < 0.0) {
        planning = 0.0;
    }
    ai->planning_score = 0.8 * ai->planning_score + 0.2 * planning;

    Formula copy = *formula;
    copy.effectiveness = fmax(0.0, fmin(1.0, experience->reward));
    copy.confirmations += 1;
    formula_collection_add(ai->library, &copy);
    update_average_reward(ai);

    KolibriAIDatasetEntry entry = {0};
    copy_string_truncated(entry.prompt, sizeof(entry.prompt), formula->content);
    snprintf(entry.response, sizeof(entry.response), "%.3f", experience->reward);
    entry.reward = experience->reward;
    entry.poe = experience->poe;
    entry.mdl = experience->mdl;
    entry.timestamp = time(NULL);
    dataset_append(&ai->dataset, &entry);
    dataset_trim(&ai->dataset, ai->snapshot_limit);

    KolibriMemoryFact fact = {0};
    copy_string_truncated(fact.key, sizeof(fact.key), formula->id);
    copy_string_truncated(fact.value, sizeof(fact.value), formula->content);
    fact.salience = experience->reward;
    fact.last_updated = entry.timestamp;
    memory_record(&ai->memory, &fact, ai->snapshot_limit);

    pthread_mutex_unlock(&ai->mutex);
    return 0;
}

size_t kolibri_ai_get_interaction_log(const KolibriAI *ai,
                                      KolibriAISelfplayInteraction *buffer,
                                      size_t max_entries) {
    if (!ai) {
        return 0;
    }
    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    size_t available = ai->interaction_log.count;
    size_t to_copy = 0;
    if (buffer && max_entries > 0 && available > 0) {
        to_copy = available < max_entries ? available : max_entries;
        size_t start = available > to_copy ? available - to_copy : 0;
        memcpy(buffer,
               ai->interaction_log.entries + start,
               to_copy * sizeof(*buffer));
    }
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
    return available;
}

int kolibri_ai_replay_log(KolibriAI *ai,
                          double *out_max_abs_error,
                          double *out_average_reward) {
    if (!ai) {
        return -1;
    }
    pthread_mutex_lock(&ai->mutex);
    size_t count = ai->interaction_log.count;
    KolibriAISelfplayInteraction *copy = NULL;
    if (count > 0) {
        copy = malloc(count * sizeof(*copy));
        if (!copy) {
            pthread_mutex_unlock(&ai->mutex);
            return -1;
        }
        memcpy(copy,
               ai->interaction_log.entries,
               count * sizeof(*copy));
    }
    pthread_mutex_unlock(&ai->mutex);

    double max_error = 0.0;
    double reward_sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        uint8_t *code = NULL;
        size_t code_len = 0;
        if (compile_selfplay_program(&copy[i].task, &code, &code_len) != 0) {
            free(copy);
            return -1;
        }
        prog_t prog = {code, code_len};
        vm_limits_t limits = {256, 64};
        vm_result_t result = {0};
        if (vm_run(&prog, &limits, NULL, &result) != 0) {
            free(code);
            free(copy);
            return -1;
        }
        double predicted = (double)result.result;
        double diff = fabs(predicted - copy[i].predicted_result);
        if (diff > max_error) {
            max_error = diff;
        }
        reward_sum += compute_selfplay_reward(&copy[i].task, predicted);
        free(code);
    }
    if (out_max_abs_error) {
        *out_max_abs_error = max_error;
    }
    if (out_average_reward) {
        *out_average_reward = (count > 0) ? (reward_sum / (double)count) : 0.0;
    }
    free(copy);
    return 0;
}

int kolibri_ai_add_formula(KolibriAI *ai, const Formula *formula) {
    if (!ai || !formula) {
        return -1;
    }
    pthread_mutex_lock(&ai->mutex);
    int rc = formula_collection_add(ai->library, formula);
    if (rc == 0) {
        update_average_reward(ai);
    }
    pthread_mutex_unlock(&ai->mutex);
    return rc;
}

Formula *kolibri_ai_get_best_formula(KolibriAI *ai) {
    if (!ai || !ai->library) {
        return NULL;
    }
    pthread_mutex_lock(&ai->mutex);
    Formula *best = NULL;
    double best_score = -1.0;
    for (size_t i = 0; i < ai->library->count; ++i) {
        if (ai->library->formulas[i].effectiveness > best_score) {
            best_score = ai->library->formulas[i].effectiveness;
            best = &ai->library->formulas[i];
        }
    }
    pthread_mutex_unlock(&ai->mutex);
    return best;
}

static char *alloc_json_buffer(size_t initial) {
    char *buffer = malloc(initial);
    if (buffer) {
        buffer[0] = '\0';
    }
    return buffer;
}

char *kolibri_ai_serialize_state(const KolibriAI *ai) {
    if (!ai) {
        return NULL;
    }
    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    size_t count = ai->library ? ai->library->count : 0;
    double average = ai->average_reward;
    double planning = ai->planning_score;
    double poe = ai->recent_poe;
    double mdl = ai->recent_mdl;
    uint64_t iterations = ai->iterations;
    size_t queue_depth = ai->job_queue.count;
    size_t dataset_size = ai->dataset.count;
    KolibriCurriculumState curriculum = ai->curriculum;
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    size_t needed = 512 + KOLIBRI_DIFFICULTY_COUNT * 96;
    char *json = alloc_json_buffer(needed);
    if (!json) {
        return NULL;
    }

    size_t offset = (size_t)snprintf(json,
                                     needed,
                                     "{\"iterations\":%llu,\"formula_count\":%zu,"
                                     "\"average_reward\":%.6f,\"planning_score\":%.6f,"
                                     "\"recent_poe\":%.6f,\"recent_mdl\":%.6f,"
                                     "\"queue_depth\":%zu,\"dataset_size\":%zu,"
                                     "\"curriculum_temperature\":%.6f,"
                                     "\"curriculum_level\":%d,"
                                     "\"curriculum_distribution\":[",
                                     (unsigned long long)iterations,
                                     count,
                                     average,
                                     planning,
                                     poe,
                                     mdl,
                                     queue_depth,
                                     dataset_size,
                                     curriculum.temperature,
                                     (int)curriculum.current_level);
    if (offset >= needed) {
        json[needed - 1] = '\0';
        return json;
    }

    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT && offset < needed; ++i) {
        int written = snprintf(json + offset,
                               needed - offset,
                               "%s%.6f",
                               i == 0 ? "" : ",",
                               curriculum.distribution[i]);
        if (written < 0) {
            json[0] = '\0';
            return json;
        }
        offset += (size_t)written;
    }

    if (offset < needed) {
        int written = snprintf(json + offset,
                               needed - offset,
                               "],\"curriculum_success\":[");
        if (written < 0) {
            json[0] = '\0';
            return json;
        }
        offset += (size_t)written;
    }

    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT && offset < needed; ++i) {
        int written = snprintf(json + offset,
                               needed - offset,
                               "%s%.6f",
                               i == 0 ? "" : ",",
                               curriculum.success_ema[i]);
        if (written < 0) {
            json[0] = '\0';
            return json;
        }
        offset += (size_t)written;
    }

    if (offset < needed) {
        int written = snprintf(json + offset,
                               needed - offset,
                               "],\"curriculum_samples\":[");
        if (written < 0) {
            json[0] = '\0';
            return json;
        }
        offset += (size_t)written;
    }

    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT && offset < needed; ++i) {
        int written = snprintf(json + offset,
                               needed - offset,
                               "%s%llu",
                               i == 0 ? "" : ",",
                               (unsigned long long)curriculum.sample_count[i]);
        if (written < 0) {
            json[0] = '\0';
            return json;
        }
        offset += (size_t)written;
    }

    if (offset < needed) {
        snprintf(json + offset,
                 needed - offset,
                 "],\"global_success\":%.6f}",
                 curriculum.global_success_ema);
    } else if (needed > 0) {
        json[needed - 1] = '\0';
    }

    return json;
}

char *kolibri_ai_serialize_formulas(const KolibriAI *ai, size_t max_results) {
    if (!ai || !ai->library) {
        return NULL;
    }
    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    size_t count = ai->library->count;
    if (max_results == 0 || max_results > count) {
        max_results = count;
    }
    size_t needed = 128 + max_results * 160;
    char *json = alloc_json_buffer(needed);
    if (!json) {
        pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
        return NULL;
    }
    size_t offset = (size_t)snprintf(json, needed, "{\"formulas\":[");
    for (size_t i = 0; i < max_results && offset < needed; ++i) {
        const Formula *formula = &ai->library->formulas[i];
        int written = snprintf(json + offset,
                               needed - offset,
                               "%s{\"id\":\"%s\",\"effectiveness\":%.6f}",
                               i == 0 ? "" : ",",
                               formula->id,
                               formula->effectiveness);
        if (written < 0) {
            json[0] = '\0';
            break;
        }
        offset += (size_t)written;
    }
    if (offset < needed) {
        snprintf(json + offset, needed - offset, "]}");
    } else if (needed > 0) {
        json[needed - 1] = '\0';
    }
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
    return json;
}

char *kolibri_ai_export_snapshot(const KolibriAI *ai) {
    if (!ai) {
        return NULL;
    }
    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    size_t count = ai->library ? ai->library->count : 0;
    size_t formulas_cap = 2; /* [] */
    if (ai->library) {
        for (size_t i = 0; i < ai->library->count; ++i) {
            size_t id_len = strnlen(ai->library->formulas[i].id,
                                    sizeof(ai->library->formulas[i].id));
            formulas_cap += id_len + 40; /* formatting overhead */
        }
    }
    char *formulas = malloc(formulas_cap);
    if (!formulas) {
        pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
        return NULL;
    }
    size_t offset = (size_t)snprintf(formulas, formulas_cap, "[");
    for (size_t i = 0; i < count && offset < formulas_cap; ++i) {
        const Formula *formula = &ai->library->formulas[i];
        int written = snprintf(formulas + offset,
                               formulas_cap - offset,
                               "%s{\"id\":\"%s\",\"effectiveness\":%.6f}",
                               i == 0 ? "" : ",",
                               formula->id,
                               formula->effectiveness);
        if (written < 0) {
            formulas[0] = '\0';
            break;
        }
        offset += (size_t)written;
    }
    if (offset < formulas_cap) {
        snprintf(formulas + offset, formulas_cap - offset, "]");
    } else if (formulas_cap > 0) {
        formulas[formulas_cap - 1] = '\0';
    }

    double average = ai->average_reward;
    double planning = ai->planning_score;
    double poe = ai->recent_poe;
    double mdl = ai->recent_mdl;
    uint64_t iterations = ai->iterations;
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    size_t total = 128 + strlen(formulas);
    char *result = malloc(total);
    if (!result) {
        free(formulas);
        return NULL;
    }
    snprintf(result,
             total,
             "{\"iterations\":%llu,\"average_reward\":%.6f,"
             "\"planning_score\":%.6f,\"recent_poe\":%.6f,"
             "\"recent_mdl\":%.6f,\"formulas\":%s}",
             (unsigned long long)iterations,
             average,
             planning,
             poe,
             mdl,
             formulas);
    free(formulas);
    return result;
}

static int parse_double_field(const char *json, const char *key, double *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *pos = strstr(json, pattern);
    if (!pos) {
        return -1;
    }
    pos += strlen(pattern);
    char *end = NULL;
    double value = strtod(pos, &end);
    if (end == pos) {
        return -1;
    }
    *out = value;
    return 0;
}

static int parse_uint64_field(const char *json, const char *key, uint64_t *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *pos = strstr(json, pattern);
    if (!pos) {
        return -1;
    }
    pos += strlen(pattern);
    char *end = NULL;
    unsigned long long value = strtoull(pos, &end, 10);
    if (end == pos) {
        return -1;
    }
    *out = (uint64_t)value;
    return 0;
}

int kolibri_ai_import_snapshot(KolibriAI *ai, const char *json) {
    if (!ai || !json) {
        return -1;
    }

    pthread_mutex_lock(&ai->mutex);
    double tmp_double = 0.0;
    uint64_t tmp_u64 = 0;
    if (parse_uint64_field(json, "iterations", &tmp_u64) == 0) {
        ai->iterations = tmp_u64;
    }
    if (parse_double_field(json, "average_reward", &tmp_double) == 0) {
        ai->average_reward = tmp_double;
    }
    if (parse_double_field(json, "planning_score", &tmp_double) == 0) {
        ai->planning_score = tmp_double;
    }
    if (parse_double_field(json, "recent_poe", &tmp_double) == 0) {
        ai->recent_poe = tmp_double;
    }
    if (parse_double_field(json, "recent_mdl", &tmp_double) == 0) {
        ai->recent_mdl = tmp_double;
    }

    const char *array = strstr(json, "\"formulas\":[");
    if (array) {
        array += strlen("\"formulas\":[");
        const char *array_end = strchr(array, ']');
        if (array_end) {
            for (size_t i = 0; i < ai->library->count; ++i) {
                formula_clear(&ai->library->formulas[i]);
            }
            ai->library->count = 0;
            while (array < array_end) {
                const char *id_key = strstr(array, "\"id\":\"");
                if (!id_key || id_key >= array_end) {
                    break;
                }
                id_key += strlen("\"id\":\"");
                const char *id_end = strchr(id_key, '"');
                if (!id_end || id_end > array_end) {
                    break;
                }
                size_t id_len = (size_t)(id_end - id_key);
                char id_buf[64];
                if (id_len >= sizeof(id_buf)) {
                    id_len = sizeof(id_buf) - 1;
                }
                memcpy(id_buf, id_key, id_len);
                id_buf[id_len] = '\0';

                const char *eff_key = strstr(id_end, "\"effectiveness\":");
                if (!eff_key || eff_key >= array_end) {
                    break;
                }
                eff_key += strlen("\"effectiveness\":");
                char *eff_end = NULL;
                double eff_value = strtod(eff_key, &eff_end);
                if (eff_end == eff_key) {
                    break;
                }

                Formula formula = {0};
                formula.representation = FORMULA_REPRESENTATION_TEXT;
                memcpy(formula.id, id_buf, id_len);
                formula.id[id_len] = '\0';
                formula.effectiveness = eff_value;
                formula_collection_add(ai->library, &formula);
                array = eff_end;
            }
            update_average_reward(ai);
        }
    }

    pthread_mutex_unlock(&ai->mutex);
    return 0;
}

int kolibri_ai_sync_with_neighbor(KolibriAI *ai, const char *base_url) {
    if (!ai || !base_url || base_url[0] == '\0') {
        return -1;
    }

    Formula *formulas = NULL;
    KolibriAIDatasetEntry *dataset_entries = NULL;
    size_t formula_count = 0;
    size_t dataset_count = 0;

    pthread_mutex_lock(&ai->mutex);
    if (ai->library && ai->library->count > 0) {
        formulas = calloc(ai->library->count, sizeof(*formulas));
        if (!formulas) {
            pthread_mutex_unlock(&ai->mutex);
            return -1;
        }
        for (size_t i = 0; i < ai->library->count; ++i) {
            const Formula *formula = &ai->library->formulas[i];
            if (formula->effectiveness >= KOLIBRI_POE_THRESHOLD) {
                formulas[formula_count++] = *formula;
            }
        }
    }

    if (ai->dataset.count > 0) {
        size_t limit = ai->dataset.count;
        if (limit > KOLIBRI_SYNC_DATASET_LIMIT) {
            limit = KOLIBRI_SYNC_DATASET_LIMIT;
        }
        dataset_entries = calloc(limit, sizeof(*dataset_entries));
        if (!dataset_entries) {
            pthread_mutex_unlock(&ai->mutex);
            free(formulas);
            return -1;
        }
        size_t start = ai->dataset.count > limit ? ai->dataset.count - limit : 0;
        for (size_t i = 0; i < limit; ++i) {
            dataset_entries[i] = ai->dataset.entries[start + i];
        }
        dataset_count = limit;
    }
    pthread_mutex_unlock(&ai->mutex);

    size_t payload_cap = 256 + formula_count * 512 + dataset_count * 512;
    char *payload = malloc(payload_cap);
    if (!payload) {
        free(formulas);
        free(dataset_entries);
        return -1;
    }
    size_t offset = 0;
    offset += (size_t)snprintf(payload + offset,
                               payload_cap - offset,
                               "{\"programs\":[");
    for (size_t i = 0; i < formula_count && offset < payload_cap; ++i) {
        char id_buf[128];
        char content_buf[512];
        json_escape_copy(formulas[i].id, id_buf, sizeof(id_buf));
        json_escape_copy(formulas[i].content, content_buf, sizeof(content_buf));
        int written = snprintf(payload + offset,
                               payload_cap - offset,
                               "%s{\"id\":\"%s\",\"content\":\"%s\",\"poe\":%.6f,\"mdl\":%.6f}",
                               i == 0 ? "" : ",",
                               id_buf,
                               content_buf,
                               formulas[i].effectiveness,
                               0.0);
        if (written < 0) {
            offset = payload_cap;
            break;
        }
        offset += (size_t)written;
    }
    offset += (size_t)snprintf(payload + offset,
                               payload_cap - offset,
                               "],\"dataset\":[");
    for (size_t i = 0; i < dataset_count && offset < payload_cap; ++i) {
        char prompt_buf[512];
        char response_buf[512];
        json_escape_copy(dataset_entries[i].prompt, prompt_buf, sizeof(prompt_buf));
        json_escape_copy(dataset_entries[i].response, response_buf, sizeof(response_buf));
        int written = snprintf(payload + offset,
                               payload_cap - offset,
                               "%s{\"prompt\":\"%s\",\"response\":\"%s\",\"poe\":%.6f}",
                               i == 0 ? "" : ",",
                               prompt_buf,
                               response_buf,
                               dataset_entries[i].poe);
        if (written < 0) {
            offset = payload_cap;
            break;
        }
        offset += (size_t)written;
    }
    if (offset < payload_cap) {
        snprintf(payload + offset, payload_cap - offset, "]}");
    } else {
        payload[payload_cap - 1] = '\0';
    }

    free(formulas);
    free(dataset_entries);

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(payload);
        return -1;
    }
    char url[512];
    size_t base_len = strlen(base_url);
    if (base_len + 12 >= sizeof(url)) {
        curl_easy_cleanup(curl);
        free(payload);
        return -1;
    }
    snprintf(url,
             sizeof(url),
             "%s%sswarm/sync",
             base_url,
             base_url[base_len - 1] == '/' ? "" : "/");

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_buffer_t response = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(payload));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, kolibri_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(payload);

    if (res != CURLE_OK || status >= 400) {
        free(response.data);
        return -1;
    }

    if (response.data && response.size > 0) {
        const char *programs_end = NULL;
        const char *programs = json_find_array(response.data, "programs", &programs_end);
        const char *cursor = programs;
        while (cursor && cursor < programs_end) {
            const char *id_pos = strstr(cursor, "\"id\":\"");
            if (!id_pos || id_pos >= programs_end) {
                break;
            }
            id_pos += strlen("\"id\":\"");
            const char *id_end = strchr(id_pos, '"');
            if (!id_end || id_end > programs_end) {
                break;
            }
            char id[64];
            size_t id_len = (size_t)(id_end - id_pos);
            if (id_len >= sizeof(id)) {
                id_len = sizeof(id) - 1;
            }
            memcpy(id, id_pos, id_len);
            id[id_len] = '\0';

            const char *content_pos = strstr(id_end, "\"content\":\"");
            if (!content_pos || content_pos >= programs_end) {
                break;
            }
            content_pos += strlen("\"content\":\"");
            const char *content_end = strchr(content_pos, '"');
            if (!content_end || content_end > programs_end) {
                break;
            }
            char content[256];
            size_t content_len = (size_t)(content_end - content_pos);
            if (content_len >= sizeof(content)) {
                content_len = sizeof(content) - 1;
            }
            memcpy(content, content_pos, content_len);
            content[content_len] = '\0';

            const char *poe_pos = strstr(content_end, "\"poe\":");
            if (!poe_pos || poe_pos >= programs_end) {
                break;
            }
            poe_pos += strlen("\"poe\":");
            double remote_poe = strtod(poe_pos, NULL);

            double remote_mdl = 0.0;
            const char *mdl_pos = strstr(content_end, "\"mdl\":");
            if (mdl_pos && mdl_pos < programs_end) {
                mdl_pos += strlen("\"mdl\":");
                remote_mdl = strtod(mdl_pos, NULL);
            }

            if (remote_poe >= KOLIBRI_POE_THRESHOLD) {
                Formula formula = {0};
                formula.representation = FORMULA_REPRESENTATION_TEXT;
                copy_string_truncated(formula.id, sizeof(formula.id), id);
                copy_string_truncated(formula.content, sizeof(formula.content), content);
                formula.effectiveness = remote_poe;
                formula.created_at = time(NULL);
                formula.tests_passed = 1;
                formula.confirmations = 1;

                FormulaExperience exp = {0};
                exp.reward = remote_poe;
                exp.poe = remote_poe;
                exp.mdl = remote_mdl;
                exp.imitation_score = remote_poe;
                exp.accuracy = remote_poe;
                snprintf(exp.source, sizeof(exp.source), "sync");
                copy_string_truncated(exp.task_id, sizeof(exp.task_id), id);
                kolibri_ai_apply_reinforcement(ai, &formula, &exp);
            }

            const char *next = strchr(content_end, '}');
            if (!next || next >= programs_end) {
                break;
            }
            cursor = next + 1;
        }

        const char *dataset_end = NULL;
        const char *dataset_json = json_find_array(response.data, "dataset", &dataset_end);
        cursor = dataset_json;
        size_t entry_index = 0;
        while (cursor && cursor < dataset_end) {
            const char *prompt_pos = strstr(cursor, "\"prompt\":\"");
            if (!prompt_pos || prompt_pos >= dataset_end) {
                break;
            }
            prompt_pos += strlen("\"prompt\":\"");
            const char *prompt_end = strchr(prompt_pos, '"');
            if (!prompt_end || prompt_end > dataset_end) {
                break;
            }
            char prompt[256];
            size_t prompt_len = (size_t)(prompt_end - prompt_pos);
            if (prompt_len >= sizeof(prompt)) {
                prompt_len = sizeof(prompt) - 1;
            }
            memcpy(prompt, prompt_pos, prompt_len);
            prompt[prompt_len] = '\0';

            const char *response_pos = strstr(prompt_end, "\"response\":\"");
            if (!response_pos || response_pos >= dataset_end) {
                break;
            }
            response_pos += strlen("\"response\":\"");
            const char *response_end = strchr(response_pos, '"');
            if (!response_end || response_end > dataset_end) {
                break;
            }
            char response_text[256];
            size_t response_len = (size_t)(response_end - response_pos);
            if (response_len >= sizeof(response_text)) {
                response_len = sizeof(response_text) - 1;
            }
            memcpy(response_text, response_pos, response_len);
            response_text[response_len] = '\0';

            const char *poe_pos = strstr(response_end, "\"poe\":");
            if (!poe_pos || poe_pos >= dataset_end) {
                break;
            }
            poe_pos += strlen("\"poe\":");
            double remote_poe = strtod(poe_pos, NULL);

            KolibriAIDatasetEntry entry = {0};
            copy_string_truncated(entry.prompt, sizeof(entry.prompt), prompt);
            copy_string_truncated(entry.response, sizeof(entry.response), response_text);
            entry.reward = remote_poe;
            entry.poe = remote_poe;
            entry.mdl = 0.0;
            entry.timestamp = time(NULL);

            pthread_mutex_lock(&ai->mutex);
            dataset_append(&ai->dataset, &entry);
            dataset_trim(&ai->dataset, ai->snapshot_limit);
            KolibriMemoryFact fact = {0};
            snprintf(fact.key, sizeof(fact.key), "sync.dataset.%zu", entry_index++);
            copy_string_truncated(fact.value, sizeof(fact.value), prompt);
            fact.salience = remote_poe;
            fact.last_updated = entry.timestamp;
            memory_record(&ai->memory, &fact, ai->snapshot_limit);
            pthread_mutex_unlock(&ai->mutex);

            const char *next = strchr(response_end, '}');
            if (!next || next >= dataset_end) {
                break;
            }
            cursor = next + 1;
        }
    }

    free(response.data);
    return 0;
}
