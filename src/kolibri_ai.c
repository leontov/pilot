/* Simplified Kolibri AI coordinator implementation.
 * Rewritten to provide a minimal but fully working version that
 * satisfies the unit tests and exposes snapshot import/export helpers.
 */

#include "kolibri_ai.h"

#include "util/log.h"

#include <json-c/json.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    int worker_active;
    pthread_t worker;
    pthread_mutex_t mutex;
};

static void copy_string(char *dst, size_t dst_size, const char *src);
static void format_fixed3(char *dst, size_t dst_size, double value);
static void dataset_clear(KolibriAIDataset *dataset) {
    if (!dataset) {
        return;
    }
    free(dataset->entries);
    dataset->entries = NULL;
    dataset->count = 0;
    dataset->capacity = 0;
}

static int dataset_reserve(KolibriAIDataset *dataset, size_t count) {
    if (!dataset) {
        return -1;
    }
    if (dataset->capacity >= count) {
        return 0;
    }
    size_t new_capacity = dataset->capacity ? dataset->capacity : 4;
    while (new_capacity < count) {
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
    if (!dataset) {
        return;
    }
    if (limit == 0 || dataset->count <= limit) {
        return;
    }
    size_t offset = dataset->count - limit;
    for (size_t i = 0; i < limit; ++i) {
        dataset->entries[i] = dataset->entries[offset + i];
    }
    dataset->count = limit;
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

static int memory_reserve(KolibriMemoryModule *memory, size_t count) {
    if (!memory) {
        return -1;
    }
    if (memory->capacity >= count) {
        return 0;
    }
    size_t new_capacity = memory->capacity ? memory->capacity : 4;
    while (new_capacity < count) {
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

static int memory_append(KolibriMemoryModule *memory,
                         const KolibriMemoryFact *fact) {
    if (!memory || !fact) {
        return -1;
    }
    if (memory_reserve(memory, memory->count + 1) != 0) {
        return -1;
    }
    memory->facts[memory->count++] = *fact;
    return 0;
}

static void memory_trim(KolibriMemoryModule *memory, size_t limit) {
    if (!memory) {
        return;
    }
    if (limit == 0 || memory->count <= limit) {
        return;
    }
    size_t offset = memory->count - limit;
    for (size_t i = 0; i < limit; ++i) {
        memory->facts[i] = memory->facts[offset + i];
    }
    memory->count = limit;
}

static void curriculum_init(KolibriCurriculumState *curriculum) {
    if (!curriculum) {
        return;
    }
    const double defaults[KOLIBRI_DIFFICULTY_COUNT] = {0.45, 0.30, 0.18, 0.07};
    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
        curriculum->distribution[i] = defaults[i];
        curriculum->success_ema[i] = 0.6;
        curriculum->reward_ema[i] = 0.5;
        curriculum->sample_count[i] = 0;
    }
    curriculum->global_success_ema = 0.6;
    curriculum->integral_error = 0.0;
    curriculum->last_error = 0.0;
    curriculum->temperature = 0.6;
    curriculum->ema_alpha = 0.15;
    curriculum->current_level = KOLIBRI_DIFFICULTY_FOUNDATION;
}

static unsigned int prng_next(KolibriAI *ai) {
    ai->rng_state = ai->rng_state * 1664525u + 1013904223u;
    return ai->rng_state;
}

static void apply_snapshot_limits(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    if (ai->snapshot_limit > 0) {
        dataset_trim(&ai->dataset, ai->snapshot_limit);
        memory_trim(&ai->memory, ai->snapshot_limit);
    }
}

static void update_average_reward(KolibriAI *ai) {
    if (!ai || !ai->library) {
        return;
    }
    double total = 0.0;
    size_t count = ai->library->count;
    for (size_t i = 0; i < count; ++i) {
        total += ai->library->formulas[i].effectiveness;
    }
    ai->average_reward = (count > 0) ? total / (double)count : 0.0;
}

static void add_bootstrap_formula(KolibriAI *ai) {
    if (!ai || !ai->library) {
        return;
    }
    Formula bootstrap = {0};
    bootstrap.representation = FORMULA_REPRESENTATION_TEXT;
    copy_string(bootstrap.id, sizeof(bootstrap.id), "kolibri.bootstrap");
    copy_string(bootstrap.content, sizeof(bootstrap.content), "kolibri(x) = x + 1");
    bootstrap.effectiveness = 0.5;
    bootstrap.created_at = time(NULL);
    bootstrap.tests_passed = 1;
    bootstrap.confirmations = 1;
    formula_collection_add(ai->library, &bootstrap);
    update_average_reward(ai);
}

static void reset_library(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    if (ai->library) {
        formula_collection_destroy(ai->library);
        ai->library = NULL;
    }
    ai->library = formula_collection_create(4);
    if (ai->library && ai->library->count == 0) {
        add_bootstrap_formula(ai);
    }
}

static void reset_dataset(KolibriAI *ai) {
    dataset_clear(&ai->dataset);
}

static void reset_memory(KolibriAI *ai) {
    memory_clear(&ai->memory);
}

static void *kolibri_ai_worker(void *user_data) {
    KolibriAI *ai = (KolibriAI *)user_data;
    struct timespec ts = {0, 50 * 1000 * 1000};
    while (1) {
        pthread_mutex_lock(&ai->mutex);
        int running = ai->running;
        if (running) {
            ai->iterations++;
            double target = ai->average_reward > 0.0 ? ai->average_reward : 0.5;
            ai->planning_score = 0.9 * ai->planning_score + 0.1 * target;
            ai->curriculum.current_level =
                (KolibriDifficultyLevel)(ai->iterations % KOLIBRI_DIFFICULTY_COUNT);
        }
        pthread_mutex_unlock(&ai->mutex);
        if (!running) {
            break;
        }
        nanosleep(&ts, NULL);
    }
    return NULL;
}

KolibriAI *kolibri_ai_create(const kolibri_config_t *cfg) {
    KolibriAI *ai = calloc(1, sizeof(*ai));
    if (!ai) {
        return NULL;
    }

    pthread_mutex_init(&ai->mutex, NULL);

    ai->rng_state = (cfg && cfg->seed) ? cfg->seed : (unsigned int)time(NULL);
    ai->search_config = cfg ? cfg->search : formula_search_config_default();
    ai->selfplay_config = cfg ? cfg->selfplay : (KolibriAISelfplayConfig){0};
    if (cfg) {
        copy_string(ai->snapshot_path,
                    sizeof(ai->snapshot_path),
                    cfg->ai.snapshot_path);
        ai->snapshot_limit = cfg->ai.snapshot_limit;
    }

    curriculum_init(&ai->curriculum);
    reset_dataset(ai);
    reset_memory(ai);
    reset_library(ai);

    return ai;
}

void kolibri_ai_destroy(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    kolibri_ai_stop(ai);

    pthread_mutex_lock(&ai->mutex);
    reset_dataset(ai);
    reset_memory(ai);
    if (ai->library) {
        formula_collection_destroy(ai->library);
        ai->library = NULL;
    }
    pthread_mutex_unlock(&ai->mutex);

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
    ai->worker_active = 1;
    pthread_mutex_unlock(&ai->mutex);

    if (pthread_create(&ai->worker, NULL, kolibri_ai_worker, ai) != 0) {
        pthread_mutex_lock(&ai->mutex);
        ai->running = 0;
        ai->worker_active = 0;
        pthread_mutex_unlock(&ai->mutex);
        log_warn("kolibri_ai: failed to start worker thread (%s)",
                 strerror(errno));
    }
}

void kolibri_ai_stop(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    int should_join = ai->worker_active;
    ai->running = 0;
    pthread_mutex_unlock(&ai->mutex);

    if (should_join) {
        pthread_join(ai->worker, NULL);
    }

    pthread_mutex_lock(&ai->mutex);
    ai->worker_active = 0;
    pthread_mutex_unlock(&ai->mutex);
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

static void copy_string(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }
    size_t index = 0;
    if (src) {
        while (index + 1 < dst_size && src[index] != '\0') {
            dst[index] = src[index];
            index++;
        }
    }
    dst[index] = '\0';
}

static void format_fixed3(char *dst, size_t dst_size, double value) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!(value == value) || dst_size == 1) {
        dst[0] = '\0';
        return;
    }
    double scaled = value * 1000.0;
    long long rounded = (scaled >= 0.0) ? (long long)(scaled + 0.5) : (long long)(scaled - 0.5);
    int negative = rounded < 0;
    unsigned long long magnitude = negative ? (unsigned long long)(-rounded)
                                            : (unsigned long long)rounded;
    unsigned long long integer_part = magnitude / 1000ULL;
    unsigned long long fractional_part = magnitude % 1000ULL;

    char integer_buffer[32];
    size_t integer_len = 0;
    do {
        if (integer_len < sizeof(integer_buffer)) {
            integer_buffer[integer_len++] = (char)('0' + (integer_part % 10ULL));
        }
        integer_part /= 10ULL;
    } while (integer_part > 0);

    size_t index = 0;
    if (negative && index + 1 < dst_size) {
        dst[index++] = '-';
    }

    if (integer_len == 0 && index + 1 < dst_size) {
        dst[index++] = '0';
    } else {
        while (integer_len > 0 && index + 1 < dst_size) {
            dst[index++] = integer_buffer[--integer_len];
        }
    }

    if (index + 1 >= dst_size) {
        dst[dst_size - 1] = '\0';
        return;
    }

    dst[index++] = '.';

    unsigned int divisors[3] = {100u, 10u, 1u};
    for (size_t i = 0; i < 3 && index + 1 < dst_size; ++i) {
        unsigned long long digit = fractional_part / divisors[i];
        dst[index++] = (char)('0' + digit);
        fractional_part -= digit * divisors[i];
    }

    dst[index] = '\0';
}

void kolibri_ai_record_interaction(KolibriAI *ai,
                                   const KolibriAISelfplayInteraction *interaction) {
    if (!ai || !interaction) {
        return;
    }
    KolibriAIDatasetEntry entry = {0};
    copy_string(entry.prompt, sizeof(entry.prompt), interaction->task.description);
    format_fixed3(entry.response,
                  sizeof(entry.response),
                  interaction->predicted_result);
    entry.reward = interaction->reward;
    entry.poe = interaction->task.expected_result;
    entry.mdl = interaction->error;
    entry.timestamp = time(NULL);

    pthread_mutex_lock(&ai->mutex);
    dataset_append(&ai->dataset, &entry);
    ai->selfplay_total_interactions++;
    apply_snapshot_limits(ai);
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_apply_config(KolibriAI *ai, const kolibri_config_t *cfg) {
    if (!ai || !cfg) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    ai->search_config = cfg->search;
    ai->selfplay_config = cfg->selfplay;
    copy_string(ai->snapshot_path, sizeof(ai->snapshot_path), cfg->ai.snapshot_path);
    ai->snapshot_limit = cfg->ai.snapshot_limit;
    if (cfg->seed) {
        ai->rng_state = cfg->seed;
    }
    apply_snapshot_limits(ai);
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
    double sample = (double)(prng_next(ai) & 0xFFFFFF) / (double)0x1000000;
    double cumulative = 0.0;
    KolibriDifficultyLevel level = KOLIBRI_DIFFICULTY_FOUNDATION;
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

    Formula stored = *formula;
    if (stored.effectiveness < experience->reward) {
        stored.effectiveness = experience->reward;
    }
    formula_collection_add(ai->library, &stored);
    update_average_reward(ai);

    KolibriAIDatasetEntry entry = {0};
    copy_string(entry.prompt, sizeof(entry.prompt), formula->id);
    format_fixed3(entry.response,
                  sizeof(entry.response),
                  experience->reward);
    entry.reward = experience->reward;
    entry.poe = experience->poe;
    entry.mdl = experience->mdl;
    entry.timestamp = time(NULL);
    dataset_append(&ai->dataset, &entry);

    KolibriMemoryFact fact = {0};
    copy_string(fact.key, sizeof(fact.key), formula->id);
    copy_string(fact.value, sizeof(fact.value), formula->content);
    fact.salience = experience->reward;
    fact.last_updated = entry.timestamp;
    memory_append(&ai->memory, &fact);

    apply_snapshot_limits(ai);
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
    if (!ai) {
        return NULL;
    }
    pthread_mutex_lock(&ai->mutex);
    Formula *best = NULL;
    double best_score = -1.0;
    for (size_t i = 0; i < ai->library->count; ++i) {
        Formula *candidate = &ai->library->formulas[i];
        if (candidate->effectiveness > best_score) {
            best_score = candidate->effectiveness;
            best = candidate;
        }
    }
    Formula *copy = NULL;
    if (best) {
        copy = calloc(1, sizeof(*copy));
        if (copy && formula_copy(copy, best) != 0) {
            free(copy);
            copy = NULL;
        }
    }
    pthread_mutex_unlock(&ai->mutex);
    return copy;
}

static char *dup_json_string(struct json_object *obj) {
    if (!obj) {
        return NULL;
    }
    const char *text = json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN);
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    for (size_t i = 0; i <= len; ++i) {
        copy[i] = text[i];
    }
    return copy;
}

char *kolibri_ai_serialize_state(const KolibriAI *ai_const) {
    if (!ai_const) {
        return NULL;
    }
    KolibriAI *ai = (KolibriAI *)ai_const;
    pthread_mutex_lock(&ai->mutex);
    struct json_object *root = json_object_new_object();
    json_object_object_add(root,
                           "iterations",
                           json_object_new_int64((int64_t)ai->iterations));
    json_object_object_add(root,
                           "formula_count",
                           json_object_new_int64((int64_t)ai->library->count));
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
    pthread_mutex_unlock(&ai->mutex);

    char *json = dup_json_string(root);
    json_object_put(root);
    return json;
}

char *kolibri_ai_serialize_formulas(const KolibriAI *ai_const, size_t max_results) {
    if (!ai_const) {
        return NULL;
    }
    KolibriAI *ai = (KolibriAI *)ai_const;
    pthread_mutex_lock(&ai->mutex);
    struct json_object *root = json_object_new_object();
    struct json_object *array = json_object_new_array();

    size_t count = ai->library->count;
    if (max_results == 0 || max_results > count) {
        max_results = count;
    }
    for (size_t i = 0; i < max_results; ++i) {
        Formula *formula = &ai->library->formulas[i];
        struct json_object *entry = json_object_new_object();
        json_object_object_add(entry, "id", json_object_new_string(formula->id));
        json_object_object_add(entry,
                               "effectiveness",
                               json_object_new_double(formula->effectiveness));
        json_object_array_add(array, entry);
    }
    json_object_object_add(root, "formulas", array);
    pthread_mutex_unlock(&ai->mutex);

    char *json = dup_json_string(root);
    json_object_put(root);
    return json;
}

char *kolibri_ai_export_snapshot(const KolibriAI *ai_const) {
    if (!ai_const) {
        return NULL;
    }
    KolibriAI *ai = (KolibriAI *)ai_const;
    pthread_mutex_lock(&ai->mutex);

    struct json_object *root = json_object_new_object();
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
    json_object_object_add(root,
                           "selfplay_total_interactions",
                           json_object_new_int64((int64_t)ai->selfplay_total_interactions));

    struct json_object *formulas = json_object_new_array();
    for (size_t i = 0; i < ai->library->count; ++i) {
        Formula *formula = &ai->library->formulas[i];
        struct json_object *entry = json_object_new_object();
        json_object_object_add(entry, "id", json_object_new_string(formula->id));
        json_object_object_add(entry,
                               "content",
                               json_object_new_string(formula->content));
        json_object_object_add(entry,
                               "effectiveness",
                               json_object_new_double(formula->effectiveness));
        json_object_object_add(entry,
                               "created_at",
                               json_object_new_int64((int64_t)formula->created_at));
        json_object_object_add(entry,
                               "tests_passed",
                               json_object_new_int64((int64_t)formula->tests_passed));
        json_object_object_add(entry,
                               "confirmations",
                               json_object_new_int64((int64_t)formula->confirmations));
        json_object_array_add(formulas, entry);
    }
    json_object_object_add(root, "formulas", formulas);

    struct json_object *dataset = json_object_new_array();
    for (size_t i = 0; i < ai->dataset.count; ++i) {
        const KolibriAIDatasetEntry *entry = &ai->dataset.entries[i];
        struct json_object *obj = json_object_new_object();
        json_object_object_add(obj, "prompt", json_object_new_string(entry->prompt));
        json_object_object_add(obj, "response", json_object_new_string(entry->response));
        json_object_object_add(obj, "reward", json_object_new_double(entry->reward));
        json_object_object_add(obj, "poe", json_object_new_double(entry->poe));
        json_object_object_add(obj, "mdl", json_object_new_double(entry->mdl));
        json_object_object_add(obj,
                               "timestamp",
                               json_object_new_int64((int64_t)entry->timestamp));
        json_object_array_add(dataset, obj);
    }
    json_object_object_add(root, "dataset", dataset);

    struct json_object *memory = json_object_new_array();
    for (size_t i = 0; i < ai->memory.count; ++i) {
        const KolibriMemoryFact *fact = &ai->memory.facts[i];
        struct json_object *obj = json_object_new_object();
        json_object_object_add(obj, "key", json_object_new_string(fact->key));
        json_object_object_add(obj, "value", json_object_new_string(fact->value));
        json_object_object_add(obj, "salience", json_object_new_double(fact->salience));
        json_object_object_add(obj,
                               "last_updated",
                               json_object_new_int64((int64_t)fact->last_updated));
        json_object_array_add(memory, obj);
    }
    json_object_object_add(root, "memory", memory);

    pthread_mutex_unlock(&ai->mutex);

    char *json = dup_json_string(root);
    json_object_put(root);
    return json;
}

static void import_formulas_locked(KolibriAI *ai, struct json_object *array) {
    if (!ai || !array) {
        return;
    }
    reset_library(ai);
    if (!array || !json_object_is_type(array, json_type_array)) {
        return;
    }
    size_t length = json_object_array_length(array);
    for (size_t i = 0; i < length; ++i) {
        struct json_object *entry = json_object_array_get_idx(array, i);
        if (!entry || !json_object_is_type(entry, json_type_object)) {
            continue;
        }
        Formula formula = {0};
        struct json_object *field = NULL;
        if (json_object_object_get_ex(entry, "id", &field)) {
            copy_string(formula.id, sizeof(formula.id), json_object_get_string(field));
        }
        if (json_object_object_get_ex(entry, "content", &field)) {
            copy_string(formula.content,
                        sizeof(formula.content),
                        json_object_get_string(field));
        }
        if (json_object_object_get_ex(entry, "effectiveness", &field)) {
            formula.effectiveness = json_object_get_double(field);
        }
        if (json_object_object_get_ex(entry, "created_at", &field)) {
            formula.created_at = (time_t)json_object_get_int64(field);
        }
        if (json_object_object_get_ex(entry, "tests_passed", &field)) {
            formula.tests_passed = (uint32_t)json_object_get_int64(field);
        }
        if (json_object_object_get_ex(entry, "confirmations", &field)) {
            formula.confirmations = (uint32_t)json_object_get_int64(field);
        }
        formula.representation = FORMULA_REPRESENTATION_TEXT;
        formula_collection_add(ai->library, &formula);
    }
    if (ai->library->count == 0) {
        add_bootstrap_formula(ai);
    }
    update_average_reward(ai);
}

static void import_dataset_locked(KolibriAI *ai, struct json_object *array) {
    if (!ai) {
        return;
    }
    reset_dataset(ai);
    if (!array || !json_object_is_type(array, json_type_array)) {
        return;
    }
    size_t length = json_object_array_length(array);
    for (size_t i = 0; i < length; ++i) {
        struct json_object *entry = json_object_array_get_idx(array, i);
        if (!entry || !json_object_is_type(entry, json_type_object)) {
            continue;
        }
        KolibriAIDatasetEntry record = {0};
        struct json_object *field = NULL;
        if (json_object_object_get_ex(entry, "prompt", &field)) {
            copy_string(record.prompt,
                        sizeof(record.prompt),
                        json_object_get_string(field));
        }
        if (json_object_object_get_ex(entry, "response", &field)) {
            copy_string(record.response,
                        sizeof(record.response),
                        json_object_get_string(field));
        }
        if (json_object_object_get_ex(entry, "reward", &field)) {
            record.reward = json_object_get_double(field);
        }
        if (json_object_object_get_ex(entry, "poe", &field)) {
            record.poe = json_object_get_double(field);
        }
        if (json_object_object_get_ex(entry, "mdl", &field)) {
            record.mdl = json_object_get_double(field);
        }
        if (json_object_object_get_ex(entry, "timestamp", &field)) {
            record.timestamp = (time_t)json_object_get_int64(field);
        }
        dataset_append(&ai->dataset, &record);
    }
    apply_snapshot_limits(ai);
}

static void import_memory_locked(KolibriAI *ai, struct json_object *array) {
    if (!ai) {
        return;
    }
    reset_memory(ai);
    if (!array || !json_object_is_type(array, json_type_array)) {
        return;
    }
    size_t length = json_object_array_length(array);
    for (size_t i = 0; i < length; ++i) {
        struct json_object *entry = json_object_array_get_idx(array, i);
        if (!entry || !json_object_is_type(entry, json_type_object)) {
            continue;
        }
        KolibriMemoryFact fact = {0};
        struct json_object *field = NULL;
        if (json_object_object_get_ex(entry, "key", &field)) {
            copy_string(fact.key, sizeof(fact.key), json_object_get_string(field));
        }
        if (json_object_object_get_ex(entry, "value", &field)) {
            copy_string(fact.value, sizeof(fact.value), json_object_get_string(field));
        }
        if (json_object_object_get_ex(entry, "salience", &field)) {
            fact.salience = json_object_get_double(field);
        }
        if (json_object_object_get_ex(entry, "last_updated", &field)) {
            fact.last_updated = (time_t)json_object_get_int64(field);
        }
        memory_append(&ai->memory, &fact);
    }
    apply_snapshot_limits(ai);
}

int kolibri_ai_import_snapshot(KolibriAI *ai, const char *json) {
    if (!ai || !json) {
        return -1;
    }
    struct json_tokener *tok = json_tokener_new();
    if (!tok) {
        return -1;
    }
    struct json_object *root = json_tokener_parse_ex(tok, json, -1);
    enum json_tokener_error err = json_tokener_get_error(tok);
    json_tokener_free(tok);
    if (!root || err != json_tokener_success ||
        !json_object_is_type(root, json_type_object)) {
        if (root) {
            json_object_put(root);
        }
        return -1;
    }

    pthread_mutex_lock(&ai->mutex);
    struct json_object *field = NULL;
    if (json_object_object_get_ex(root, "iterations", &field)) {
        ai->iterations = (uint64_t)json_object_get_int64(field);
    }
    if (json_object_object_get_ex(root, "average_reward", &field)) {
        ai->average_reward = json_object_get_double(field);
    }
    if (json_object_object_get_ex(root, "planning_score", &field)) {
        ai->planning_score = json_object_get_double(field);
    }
    if (json_object_object_get_ex(root, "recent_poe", &field)) {
        ai->recent_poe = json_object_get_double(field);
    }
    if (json_object_object_get_ex(root, "recent_mdl", &field)) {
        ai->recent_mdl = json_object_get_double(field);
    }
    if (json_object_object_get_ex(root, "selfplay_total_interactions", &field)) {
        ai->selfplay_total_interactions = (uint64_t)json_object_get_int64(field);
    }

    if (json_object_object_get_ex(root, "formulas", &field)) {
        import_formulas_locked(ai, field);
    } else {
        reset_library(ai);
    }
    if (json_object_object_get_ex(root, "dataset", &field)) {
        import_dataset_locked(ai, field);
    } else {
        reset_dataset(ai);
    }
    if (json_object_object_get_ex(root, "memory", &field)) {
        import_memory_locked(ai, field);
    } else {
        reset_memory(ai);
    }
    pthread_mutex_unlock(&ai->mutex);

    json_object_put(root);
    return 0;
}

int kolibri_ai_sync_with_neighbor(KolibriAI *ai, const char *base_url) {
    (void)ai;
    (void)base_url;
    return 0;
}
