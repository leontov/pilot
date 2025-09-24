/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#define _POSIX_C_SOURCE 200809L

#include "kolibri_ai.h"

#include "util/log.h"
#include <json-c/json.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    KolibriAI *ai;
} kolibri_worker_ctx_t;

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

static void dataset_trim(KolibriAIDataset *dataset, size_t limit) {
    if (!dataset || limit == 0 || dataset->count <= limit) {
        return;
    }
    size_t offset = dataset->count - limit;
    memmove(dataset->entries,
            dataset->entries + offset,
            limit * sizeof(dataset->entries[0]));
    dataset->count = limit;
}

static size_t clamp_snapshot_window(size_t total, size_t limit) {
    if (limit == 0 || total <= limit) {
        return 0;
    }
    return total - limit;
}

static int parse_dataset_entries(struct json_object *entries,
                                 size_t snapshot_limit,
                                 KolibriAIDatasetEntry **out_entries,
                                 size_t *out_count,
                                 size_t *out_capacity) {
    if (!entries || !out_entries || !out_count || !out_capacity) {
        return -1;
    }

    size_t total = json_object_array_length(entries);
    size_t start = clamp_snapshot_window(total, snapshot_limit);
    size_t capacity = total > start ? total - start : 0;

    KolibriAIDatasetEntry *buffer = NULL;
    if (capacity > 0) {
        buffer = calloc(capacity, sizeof(*buffer));
        if (!buffer) {
            return -1;
        }
    }

    size_t count = 0;
    for (size_t i = start; i < total; ++i) {
        struct json_object *entry_obj =
            json_object_array_get_idx(entries, (int)i);
        if (!json_object_is_type(entry_obj, json_type_object)) {
            continue;
        }

        KolibriAIDatasetEntry entry = {0};
        struct json_object *field = NULL;
        if (json_object_object_get_ex(entry_obj, "prompt", &field) &&
            json_object_is_type(field, json_type_string)) {
            copy_string_truncated(entry.prompt,
                                  sizeof(entry.prompt),
                                  json_object_get_string(field));
        }
        if (json_object_object_get_ex(entry_obj, "response", &field) &&
            json_object_is_type(field, json_type_string)) {
            copy_string_truncated(entry.response,
                                  sizeof(entry.response),
                                  json_object_get_string(field));
        }
        if (json_object_object_get_ex(entry_obj, "reward", &field) &&
            (json_object_is_type(field, json_type_double) ||
             json_object_is_type(field, json_type_int))) {
            entry.reward = json_object_get_double(field);
        }
        if (json_object_object_get_ex(entry_obj, "poe", &field) &&
            (json_object_is_type(field, json_type_double) ||
             json_object_is_type(field, json_type_int))) {
            entry.poe = json_object_get_double(field);
        }
        if (json_object_object_get_ex(entry_obj, "mdl", &field) &&
            (json_object_is_type(field, json_type_double) ||
             json_object_is_type(field, json_type_int))) {
            entry.mdl = json_object_get_double(field);
        }
        if (json_object_object_get_ex(entry_obj, "timestamp", &field) &&
            json_object_is_type(field, json_type_int)) {
            entry.timestamp = (time_t)json_object_get_int64(field);
        }

        if (buffer && count < capacity) {
            buffer[count++] = entry;
        }
    }

    if (count == 0) {
        free(buffer);
        buffer = NULL;
        capacity = 0;
    } else if (count < capacity) {
        KolibriAIDatasetEntry *shrunk =
            realloc(buffer, count * sizeof(*buffer));
        if (shrunk) {
            buffer = shrunk;
        }
        capacity = count;
    }

    *out_entries = buffer;
    *out_count = count;
    *out_capacity = capacity;
    return 0;
}

static int parse_memory_facts(struct json_object *facts,
                              size_t snapshot_limit,
                              KolibriMemoryFact **out_facts,
                              size_t *out_count,
                              size_t *out_capacity) {
    if (!facts || !out_facts || !out_count || !out_capacity) {
        return -1;
    }

    size_t total = json_object_array_length(facts);
    size_t start = clamp_snapshot_window(total, snapshot_limit);
    size_t capacity = total > start ? total - start : 0;

    KolibriMemoryFact *buffer = NULL;
    if (capacity > 0) {
        buffer = calloc(capacity, sizeof(*buffer));
        if (!buffer) {
            return -1;
        }
    }

    size_t count = 0;
    for (size_t i = start; i < total; ++i) {
        struct json_object *fact_obj = json_object_array_get_idx(facts, (int)i);
        if (!json_object_is_type(fact_obj, json_type_object)) {
            continue;
        }

        KolibriMemoryFact fact = {0};
        struct json_object *field = NULL;
        if (json_object_object_get_ex(fact_obj, "key", &field) &&
            json_object_is_type(field, json_type_string)) {
            copy_string_truncated(fact.key,
                                  sizeof(fact.key),
                                  json_object_get_string(field));
        }
        if (json_object_object_get_ex(fact_obj, "value", &field) &&
            json_object_is_type(field, json_type_string)) {
            copy_string_truncated(fact.value,
                                  sizeof(fact.value),
                                  json_object_get_string(field));
        }
        if (json_object_object_get_ex(fact_obj, "salience", &field) &&
            (json_object_is_type(field, json_type_double) ||
             json_object_is_type(field, json_type_int))) {
            fact.salience = json_object_get_double(field);
        }
        if (json_object_object_get_ex(fact_obj, "last_updated", &field) &&
            json_object_is_type(field, json_type_int)) {
            fact.last_updated = (time_t)json_object_get_int64(field);
        }

        if (buffer && count < capacity) {
            buffer[count++] = fact;
        }
    }

    if (count == 0) {
        free(buffer);
        buffer = NULL;
        capacity = 0;
    } else if (count < capacity) {
        KolibriMemoryFact *shrunk =
            realloc(buffer, count * sizeof(*buffer));
        if (shrunk) {
            buffer = shrunk;
        }
        capacity = count;
    }

    *out_facts = buffer;
    *out_count = count;
    *out_capacity = capacity;
    return 0;
}

static int parse_formulas(struct json_object *formulas,
                          Formula **out_formulas,
                          size_t *out_count) {
    if (!formulas || !out_formulas || !out_count) {
        return -1;
    }

    size_t count = json_object_array_length(formulas);
    Formula *buffer = NULL;
    if (count > 0) {
        buffer = calloc(count, sizeof(*buffer));
        if (!buffer) {
            return -1;
        }
    }

    size_t written = 0;
    for (size_t i = 0; i < count; ++i) {
        struct json_object *entry =
            json_object_array_get_idx(formulas, (int)i);
        if (!json_object_is_type(entry, json_type_object)) {
            continue;
        }
        struct json_object *id_obj = NULL;
        struct json_object *eff_obj = NULL;
        if (!json_object_object_get_ex(entry, "id", &id_obj) ||
            !json_object_is_type(id_obj, json_type_string)) {
            continue;
        }
        if (!json_object_object_get_ex(entry, "effectiveness", &eff_obj) ||
            (!json_object_is_type(eff_obj, json_type_double) &&
             !json_object_is_type(eff_obj, json_type_int))) {
            continue;
        }

        Formula formula = {0};
        formula.representation = FORMULA_REPRESENTATION_TEXT;
        copy_string_truncated(formula.id,
                              sizeof(formula.id),
                              json_object_get_string(id_obj));
        formula.effectiveness = json_object_get_double(eff_obj);

        if (buffer && written < count) {
            buffer[written++] = formula;
        }
    }

    if (written == 0) {
        free(buffer);
        buffer = NULL;
    } else if (written < count) {
        Formula *shrunk = realloc(buffer, written * sizeof(*buffer));
        if (shrunk) {
            buffer = shrunk;
        }
    }

    *out_formulas = buffer;
    *out_count = written;
    return 0;
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
    pthread_mutex_lock(&ai->mutex);
    ai->iterations++;
    double target = ai->average_reward > 0.0 ? ai->average_reward : 0.5;
    ai->planning_score = 0.9 * ai->planning_score + 0.1 * target;
    ai->curriculum.current_level =
        (KolibriDifficultyLevel)(ai->iterations % KOLIBRI_DIFFICULTY_COUNT);
    pthread_mutex_unlock(&ai->mutex);
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
    entry.poe = interaction->task.expected_result;
    entry.mdl = 0.0;
    entry.timestamp = time(NULL);

    pthread_mutex_lock(&ai->mutex);
    dataset_append(&ai->dataset, &entry);
    dataset_trim(&ai->dataset, ai->snapshot_limit);
    ai->selfplay_total_interactions++;
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
    KolibriDifficultyLevel level = KOLIBRI_DIFFICULTY_FOUNDATION;
    double cumulative = 0.0;
    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
        cumulative += ai->curriculum.distribution[i];
        if (sample <= cumulative) {
            level = (KolibriDifficultyLevel)i;
            break;
        }
    }
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
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    char *json = alloc_json_buffer(256);
    if (!json) {
        return NULL;
    }
    snprintf(json,
             256,
             "{\"iterations\":%llu,\"formula_count\":%zu,"
             "\"average_reward\":%.6f,\"planning_score\":%.6f,"
             "\"recent_poe\":%.6f,\"recent_mdl\":%.6f}",
             (unsigned long long)iterations,
             count,
             average,
             planning,
             poe,
             mdl);
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

    struct json_object *root = json_object_new_object();
    if (!root) {
        pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
        return NULL;
    }

    char *result = NULL;

    json_object_object_add(root,
                           "iterations",
                           json_object_new_int64((int64_t)ai->iterations));
    json_object_object_add(root,
                           "average_reward",
                           json_object_new_double(ai->average_reward));
    json_object_object_add(root,
                           "planning_score",
                           json_object_new_double(ai->planning_score));
    json_object_object_add(root,
                           "recent_poe",
                           json_object_new_double(ai->recent_poe));
    json_object_object_add(root,
                           "recent_mdl",
                           json_object_new_double(ai->recent_mdl));

    struct json_object *formulas = json_object_new_array();
    if (!formulas) {
        goto export_cleanup;
    }
    if (ai->library) {
        for (size_t i = 0; i < ai->library->count; ++i) {
            const Formula *formula = &ai->library->formulas[i];
            struct json_object *entry = json_object_new_object();
            if (!entry) {
                goto export_cleanup;
            }
            json_object_object_add(entry,
                                   "id",
                                   json_object_new_string(formula->id));
            json_object_object_add(entry,
                                   "effectiveness",
                                   json_object_new_double(formula->effectiveness));
            json_object_array_add(formulas, entry);
        }
    }
    json_object_object_add(root, "formulas", formulas);

    struct json_object *dataset_obj = json_object_new_object();
    if (!dataset_obj) {
        goto export_cleanup;
    }
    struct json_object *entries_array = json_object_new_array();
    if (!entries_array) {
        goto export_cleanup;
    }
    size_t dataset_start = 0;
    if (ai->snapshot_limit > 0 && ai->dataset.count > ai->snapshot_limit) {
        dataset_start = ai->dataset.count - ai->snapshot_limit;
    }
    for (size_t i = dataset_start; i < ai->dataset.count; ++i) {
        const KolibriAIDatasetEntry *entry = &ai->dataset.entries[i];
        struct json_object *entry_obj = json_object_new_object();
        if (!entry_obj) {
            goto export_cleanup;
        }
        json_object_object_add(entry_obj,
                               "prompt",
                               json_object_new_string(entry->prompt));
        json_object_object_add(entry_obj,
                               "response",
                               json_object_new_string(entry->response));
        json_object_object_add(entry_obj,
                               "reward",
                               json_object_new_double(entry->reward));
        json_object_object_add(entry_obj,
                               "poe",
                               json_object_new_double(entry->poe));
        json_object_object_add(entry_obj,
                               "mdl",
                               json_object_new_double(entry->mdl));
        json_object_object_add(entry_obj,
                               "timestamp",
                               json_object_new_int64((int64_t)entry->timestamp));
        json_object_array_add(entries_array, entry_obj);
    }
    json_object_object_add(dataset_obj, "entries", entries_array);
    json_object_object_add(root, "dataset", dataset_obj);

    struct json_object *memory_obj = json_object_new_object();
    if (!memory_obj) {
        goto export_cleanup;
    }
    struct json_object *facts_array = json_object_new_array();
    if (!facts_array) {
        goto export_cleanup;
    }
    size_t memory_start = 0;
    if (ai->snapshot_limit > 0 && ai->memory.count > ai->snapshot_limit) {
        memory_start = ai->memory.count - ai->snapshot_limit;
    }
    for (size_t i = memory_start; i < ai->memory.count; ++i) {
        const KolibriMemoryFact *fact = &ai->memory.facts[i];
        struct json_object *fact_obj = json_object_new_object();
        if (!fact_obj) {
            goto export_cleanup;
        }
        json_object_object_add(fact_obj,
                               "key",
                               json_object_new_string(fact->key));
        json_object_object_add(fact_obj,
                               "value",
                               json_object_new_string(fact->value));
        json_object_object_add(fact_obj,
                               "salience",
                               json_object_new_double(fact->salience));
        json_object_object_add(fact_obj,
                               "last_updated",
                               json_object_new_int64((int64_t)fact->last_updated));
        json_object_array_add(facts_array, fact_obj);
    }
    json_object_object_add(memory_obj, "facts", facts_array);
    json_object_object_add(root, "memory", memory_obj);

    {
        const char *json_str =
            json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
        if (!json_str) {
            goto export_cleanup;
        }
        result = strdup(json_str);
    }

export_cleanup:
    json_object_put(root);
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
    return result;
}

int kolibri_ai_import_snapshot(KolibriAI *ai, const char *json) {
    if (!ai || !json) {
        return -1;
    }

    struct json_tokener *tok = json_tokener_new();
    if (!tok) {
        return -1;
    }

    struct json_object *root =
        json_tokener_parse_ex(tok, json, (int)strlen(json));
    enum json_tokener_error err = json_tokener_get_error(tok);
    json_tokener_free(tok);
    if (!root || err != json_tokener_success ||
        !json_object_is_type(root, json_type_object)) {
        if (root) {
            json_object_put(root);
        }
        return -1;
    }

    KolibriAIDatasetEntry *new_entries = NULL;
    KolibriMemoryFact *new_facts = NULL;
    Formula *new_formulas = NULL;
    size_t new_entry_count = 0;
    size_t new_entry_capacity = 0;
    size_t new_fact_count = 0;
    size_t new_fact_capacity = 0;
    size_t new_formula_count = 0;

    int dataset_present = 0;
    int memory_present = 0;
    int formulas_present = 0;

    int has_iterations = 0;
    uint64_t parsed_iterations = 0;
    int has_average = 0;
    double parsed_average = 0.0;
    int has_planning = 0;
    double parsed_planning = 0.0;
    int has_recent_poe = 0;
    double parsed_recent_poe = 0.0;
    int has_recent_mdl = 0;
    double parsed_recent_mdl = 0.0;

    uint32_t snapshot_limit = 0;
    pthread_mutex_lock(&ai->mutex);
    snapshot_limit = ai->snapshot_limit;
    pthread_mutex_unlock(&ai->mutex);

    struct json_object *value = NULL;
    if (json_object_object_get_ex(root, "iterations", &value) &&
        json_object_is_type(value, json_type_int)) {
        parsed_iterations = (uint64_t)json_object_get_int64(value);
        has_iterations = 1;
    }
    if (json_object_object_get_ex(root, "average_reward", &value) &&
        (json_object_is_type(value, json_type_double) ||
         json_object_is_type(value, json_type_int))) {
        parsed_average = json_object_get_double(value);
        has_average = 1;
    }
    if (json_object_object_get_ex(root, "planning_score", &value) &&
        (json_object_is_type(value, json_type_double) ||
         json_object_is_type(value, json_type_int))) {
        parsed_planning = json_object_get_double(value);
        has_planning = 1;
    }
    if (json_object_object_get_ex(root, "recent_poe", &value) &&
        (json_object_is_type(value, json_type_double) ||
         json_object_is_type(value, json_type_int))) {
        parsed_recent_poe = json_object_get_double(value);
        has_recent_poe = 1;
    }
    if (json_object_object_get_ex(root, "recent_mdl", &value) &&
        (json_object_is_type(value, json_type_double) ||
         json_object_is_type(value, json_type_int))) {
        parsed_recent_mdl = json_object_get_double(value);
        has_recent_mdl = 1;
    }

    if (json_object_object_get_ex(root, "formulas", &value) &&
        json_object_is_type(value, json_type_array)) {
        formulas_present = 1;
        if (parse_formulas(value, &new_formulas, &new_formula_count) != 0) {
            json_object_put(root);
            return -1;
        }
    }

    if (json_object_object_get_ex(root, "dataset", &value) &&
        json_object_is_type(value, json_type_object)) {
        struct json_object *entries = NULL;
        if (json_object_object_get_ex(value, "entries", &entries) &&
            json_object_is_type(entries, json_type_array)) {
            dataset_present = 1;
            if (parse_dataset_entries(entries,
                                      snapshot_limit,
                                      &new_entries,
                                      &new_entry_count,
                                      &new_entry_capacity) != 0) {
                free(new_formulas);
                json_object_put(root);
                return -1;
            }
        }
    }

    if (json_object_object_get_ex(root, "memory", &value) &&
        json_object_is_type(value, json_type_object)) {
        struct json_object *facts = NULL;
        if (json_object_object_get_ex(value, "facts", &facts) &&
            json_object_is_type(facts, json_type_array)) {
            memory_present = 1;
            if (parse_memory_facts(facts,
                                   snapshot_limit,
                                   &new_facts,
                                   &new_fact_count,
                                   &new_fact_capacity) != 0) {
                free(new_formulas);
                free(new_entries);
                json_object_put(root);
                return -1;
            }
        }
    }

    int status = 0;
    pthread_mutex_lock(&ai->mutex);

    if (status == 0 && formulas_present) {
        size_t initial_capacity = new_formula_count > 0 ? new_formula_count : 8;
        FormulaCollection *replacement =
            formula_collection_create(initial_capacity);
        if (!replacement) {
            status = -1;
        } else {
            for (size_t i = 0; i < new_formula_count; ++i) {
                if (formula_collection_add(replacement, &new_formulas[i]) != 0) {
                    status = -1;
                    break;
                }
            }
            if (status == 0) {
                if (ai->library) {
                    formula_collection_destroy(ai->library);
                }
                ai->library = replacement;
                update_average_reward(ai);
            } else {
                formula_collection_destroy(replacement);
            }
        }
    }

    if (status == 0 && dataset_present) {
        dataset_clear(&ai->dataset);
        ai->dataset.entries = new_entries;
        ai->dataset.count = new_entry_count;
        ai->dataset.capacity = new_entry_capacity;
        new_entries = NULL;
    }

    if (status == 0 && memory_present) {
        memory_clear(&ai->memory);
        ai->memory.facts = new_facts;
        ai->memory.count = new_fact_count;
        ai->memory.capacity = new_fact_capacity;
        new_facts = NULL;
    }

    if (status == 0) {
        if (has_iterations) {
            ai->iterations = parsed_iterations;
        }
        if (has_average) {
            ai->average_reward = parsed_average;
        }
        if (has_planning) {
            ai->planning_score = parsed_planning;
        }
        if (has_recent_poe) {
            ai->recent_poe = parsed_recent_poe;
        }
        if (has_recent_mdl) {
            ai->recent_mdl = parsed_recent_mdl;
        }
    }

    pthread_mutex_unlock(&ai->mutex);
    json_object_put(root);

    free(new_entries);
    free(new_facts);
    free(new_formulas);
    return status;
}

int kolibri_ai_sync_with_neighbor(KolibriAI *ai, const char *base_url) {
    (void)ai;
    (void)base_url;
    return 0;
}
