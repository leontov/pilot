
/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#define _POSIX_C_SOURCE 200809L


#include "kolibri_ai.h"

#include "util/config.h"

#include "util/log.h"

#include <errno.h>
#include <json-c/json.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    KolibriAI *ai;
    size_t limit;
    size_t added;
} formula_search_ctx_t;


struct KolibriAI {
    FormulaCollection *library;

    FormulaTrainingPipeline *pipeline;

    KolibriCurriculumState curriculum;
    KolibriAIDataset dataset;
    KolibriMemoryModule memory;

    FormulaSearchConfig search_config;
    KolibriAISelfplayConfig selfplay_config;
    uint32_t selfplay_current_difficulty;
    uint64_t selfplay_total_interactions;
    double selfplay_reward_avg;
    size_t selfplay_recent_total;
    size_t selfplay_recent_success;


    double average_reward;
    double recent_reward;
    double exploration_rate;
    double exploitation_rate;

    double planning_score;
    double recent_poe;
    double recent_mdl;
    uint64_t iterations;

    unsigned int rng_state;

    char snapshot_path[256];
    size_t snapshot_limit;
};

static double kolibri_clamp(double value, double min_value, double max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void kolibri_curriculum_normalize(double *values, size_t count) {
    double total = 0.0;
    for (size_t i = 0; i < count; ++i) {
        if (values[i] < 0.0) {
            values[i] = 0.0;
        }
        total += values[i];
    }
    if (total <= 0.0) {
        double uniform = 1.0 / (double)count;
        for (size_t i = 0; i < count; ++i) {
            values[i] = uniform;
        }
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        values[i] /= total;
    }
}

static void kolibri_curriculum_init(KolibriCurriculumState *state) {
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

static void kolibri_ai_dataset_init(KolibriAIDataset *dataset) {
    if (!dataset) {
        return;
    }
    dataset->entries = NULL;
    dataset->count = 0;
    dataset->capacity = 0;
}

static void kolibri_ai_dataset_clear(KolibriAIDataset *dataset) {
    if (!dataset) {
        return;
    }
    free(dataset->entries);
    dataset->entries = NULL;
    dataset->count = 0;
    dataset->capacity = 0;
}

static int kolibri_ai_dataset_ensure_capacity(KolibriAIDataset *dataset, size_t needed) {
    if (dataset->capacity >= needed) {
        return 0;

    size_t iterations;
    int running;
    pthread_mutex_t mutex;
};

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;

    }
    size_t new_capacity = dataset->capacity == 0 ? 8 : dataset->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    KolibriAIDatasetEntry *entries = realloc(dataset->entries, new_capacity * sizeof(*entries));
    if (!entries) {
        return -1;
    }
    dataset->entries = entries;
    dataset->capacity = new_capacity;
    return 0;
}

static int kolibri_ai_dataset_append(KolibriAIDataset *dataset, const KolibriAIDatasetEntry *entry) {
    if (!dataset || !entry) {
        return -1;
    }
    if (kolibri_ai_dataset_ensure_capacity(dataset, dataset->count + 1) != 0) {
        return -1;
    }
    dataset->entries[dataset->count++] = *entry;
    return 0;
}

static void kolibri_ai_dataset_trim(KolibriAIDataset *dataset, size_t limit) {
    if (!dataset || limit == 0 || dataset->count <= limit) {
        return;
    }
    size_t offset = dataset->count - limit;
    memmove(dataset->entries, dataset->entries + offset, limit * sizeof(dataset->entries[0]));
    dataset->count = limit;
}

static void kolibri_memory_module_init(KolibriMemoryModule *memory) {
    if (!memory) {
        return;
    }
    memory->facts = NULL;
    memory->count = 0;
    memory->capacity = 0;
}

static void kolibri_memory_module_clear(KolibriMemoryModule *memory) {
    if (!memory) {
        return;
    }
    free(memory->facts);
    memory->facts = NULL;
    memory->count = 0;
    memory->capacity = 0;
}


static int kolibri_memory_module_ensure_capacity(KolibriMemoryModule *memory, size_t needed) {
    if (memory->capacity >= needed) {
        return 0;
    }
    size_t new_capacity = memory->capacity == 0 ? 4 : memory->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    KolibriMemoryFact *facts = realloc(memory->facts, new_capacity * sizeof(*facts));
    if (!facts) {
        return -1;
    }
    memory->facts = facts;
    memory->capacity = new_capacity;
    return 0;
}

static int kolibri_memory_module_record(KolibriMemoryModule *memory, const KolibriMemoryFact *fact) {
    if (!memory || !fact) {
        return -1;
    }
    for (size_t i = 0; i < memory->count; ++i) {
        if (strncmp(memory->facts[i].key, fact->key, sizeof(memory->facts[i].key)) == 0) {
            memory->facts[i] = *fact;
            return 0;
        }
    }
    if (kolibri_memory_module_ensure_capacity(memory, memory->count + 1) != 0) {
        return -1;
    }
    memory->facts[memory->count++] = *fact;
    return 0;
}

static unsigned int kolibri_ai_prng_next(KolibriAI *ai) {
    if (ai->rng_state == 0) {
        ai->rng_state = 0x9e3779b9u;
    }
    ai->rng_state = ai->rng_state * 1664525u + 1013904223u;
    return ai->rng_state;
}

static double kolibri_ai_rand_uniform(KolibriAI *ai) {
    unsigned int value = kolibri_ai_prng_next(ai);
    return (double)value / (double)UINT32_MAX;
}

static void kolibri_ai_seed_library(KolibriAI *ai) {
    static const struct {
        const char *id;
        const char *content;
        double effectiveness;
    } defaults[] = {
        {"kolibri.arith.decimal", "f(x, y) = x + y", 0.62},
        {"kolibri.memory.recall", "remember(city) -> answer(city)", 0.58},
        {"kolibri.pattern.sequence", "g(n) = 2*n + 1", 0.64},
    };

    if (!ai || !ai->library) {
        return;
    }

    time_t now = time(NULL);
    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); ++i) {
        Formula formula;
        memset(&formula, 0, sizeof(formula));
        formula.representation = FORMULA_REPRESENTATION_TEXT;
        strncpy(formula.id, defaults[i].id, sizeof(formula.id) - 1);
        strncpy(formula.content, defaults[i].content, sizeof(formula.content) - 1);
        formula.effectiveness = defaults[i].effectiveness;
        formula.created_at = now - (time_t)((sizeof(defaults) - i) * 120);
        formula.tests_passed = 1;
        formula.confirmations = 1;
        formula_collection_add(ai->library, &formula);
    }

    if (ai->library->count > 0) {
        double total = 0.0;
        for (size_t i = 0; i < ai->library->count; ++i) {
            total += ai->library->formulas[i].effectiveness;
        }
        ai->average_reward = total / (double)ai->library->count;
        for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
            ai->curriculum.reward_ema[i] = ai->average_reward;
        }
    }
}

static void kolibri_ai_recompute_average_locked(KolibriAI *ai) {
    if (!ai || !ai->library || ai->library->count == 0) {
        return;
    }
    double total = 0.0;
    for (size_t i = 0; i < ai->library->count; ++i) {
        total += ai->library->formulas[i].effectiveness;
    }
    ai->average_reward = total / (double)ai->library->count;
}

static void kolibri_ai_synthesise_formula_locked(KolibriAI *ai) {
    if (!ai || !ai->library) {
        return;
    }
    if (ai->library->count >= 64) {
        return;
    }
    Formula formula;
    memset(&formula, 0, sizeof(formula));
    formula.representation = FORMULA_REPRESENTATION_TEXT;
    double phase = sin((double)ai->iterations / 18.0);
    snprintf(formula.id, sizeof(formula.id), "kolibri.synthetic.%zu", ai->library->count + 1);
    snprintf(formula.content, sizeof(formula.content),
             "h_%zu(x)=%.0fx+%.0f",
             ai->library->count + 1,
             floor(phase * 5.0) + 2.0,
             floor(phase * 3.0) + 1.0);
    formula.effectiveness = kolibri_clamp(0.58 + 0.12 * phase, 0.4, 0.95);
    formula.created_at = time(NULL);
    formula.tests_passed = 1;
    formula.confirmations = 1;
    if (formula_collection_add(ai->library, &formula) == 0) {
        kolibri_ai_recompute_average_locked(ai);
    }
}

static int ensure_parent_directory(const char *path) {
    if (!path) {
        return -1;
    }
    char buffer[256];
    strncpy(buffer, path, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';
    char *slash = strrchr(buffer, '/');
    if (!slash) {
        return 0;
    }
    *slash = '\0';
    if (buffer[0] == '\0') {
        return 0;
    }
    char current[256];
    size_t pos = 0;
    for (size_t i = 0; buffer[i] != '\0'; ++i) {
        current[pos++] = buffer[i];
        current[pos] = '\0';
        if (buffer[i] == '/' && pos > 1) {
            if (mkdir(current, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
        }
    }
    if (mkdir(current, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static json_object *kolibri_ai_build_snapshot_locked(const KolibriAI *ai) {
    json_object *root = json_object_new_object();
    if (!root) {
        return NULL;
    }
    json_object_object_add(root, "iterations", json_object_new_int64((int64_t)ai->iterations));
    json_object_object_add(root, "average_reward", json_object_new_double(ai->average_reward));
    json_object_object_add(root, "recent_reward", json_object_new_double(ai->recent_reward));
    json_object_object_add(root, "planning_score", json_object_new_double(ai->planning_score));
    json_object_object_add(root, "recent_poe", json_object_new_double(ai->recent_poe));
    json_object_object_add(root, "recent_mdl", json_object_new_double(ai->recent_mdl));

    json_object *dataset = json_object_new_array();
    if (!dataset) {
        json_object_put(root);
        return NULL;
    }
    size_t start = 0;
    if (ai->snapshot_limit > 0 && ai->dataset.count > ai->snapshot_limit) {
        start = ai->dataset.count - ai->snapshot_limit;
    }
    for (size_t i = start; i < ai->dataset.count; ++i) {
        const KolibriAIDatasetEntry *entry = &ai->dataset.entries[i];
        json_object *obj = json_object_new_object();
        if (!obj) {
            json_object_put(dataset);
            json_object_put(root);
            return NULL;
        }
        json_object_object_add(obj, "prompt", json_object_new_string(entry->prompt));
        json_object_object_add(obj, "response", json_object_new_string(entry->response));
        json_object_object_add(obj, "reward", json_object_new_double(entry->reward));
        json_object_object_add(obj, "poe", json_object_new_double(entry->poe));
        json_object_object_add(obj, "mdl", json_object_new_double(entry->mdl));
        json_object_object_add(obj, "timestamp", json_object_new_int64((int64_t)entry->timestamp));
        json_object_array_add(dataset, obj);
    }
    json_object_object_add(root, "dataset", dataset);

    json_object *memory = json_object_new_array();
    if (!memory) {
        json_object_put(root);
        return NULL;
    }
    for (size_t i = 0; i < ai->memory.count; ++i) {
        const KolibriMemoryFact *fact = &ai->memory.facts[i];
        json_object *obj = json_object_new_object();
        if (!obj) {
            json_object_put(memory);
            json_object_put(root);
            return NULL;
        }
        json_object_object_add(obj, "key", json_object_new_string(fact->key));
        json_object_object_add(obj, "value", json_object_new_string(fact->value));
        json_object_object_add(obj, "salience", json_object_new_double(fact->salience));
        json_object_object_add(obj, "timestamp", json_object_new_int64((int64_t)fact->last_updated));
        json_object_array_add(memory, obj);
    }
    json_object_object_add(root, "memory", memory);

    json_object *formulas = json_object_new_array();
    if (!formulas) {
        json_object_put(root);
        return NULL;
    }
    for (size_t i = 0; i < ai->library->count; ++i) {
        const Formula *formula = &ai->library->formulas[i];
        json_object *obj = json_object_new_object();
        if (!obj) {
            json_object_put(formulas);
            json_object_put(root);
            return NULL;
        }
        json_object_object_add(obj, "id", json_object_new_string(formula->id));
        json_object_object_add(obj, "content", json_object_new_string(formula->content));
        json_object_object_add(obj, "effectiveness", json_object_new_double(formula->effectiveness));
        json_object_object_add(obj, "created_at", json_object_new_int64((int64_t)formula->created_at));
        json_object_array_add(formulas, obj);
    }
    json_object_object_add(root, "formulas", formulas);

    return root;
}

static int kolibri_ai_persist(KolibriAI *ai) {
    if (!ai || ai->snapshot_path[0] == '\0') {
        return 0;
    }
    pthread_mutex_lock(&ai->mutex);
    json_object *root = kolibri_ai_build_snapshot_locked(ai);
    pthread_mutex_unlock(&ai->mutex);
    if (!root) {
        return -1;
    }
    const char *json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    if (!json_str) {
        json_object_put(root);
        return -1;
    }
    if (ensure_parent_directory(ai->snapshot_path) != 0) {
        json_object_put(root);
        return -1;
    }
    FILE *fp = fopen(ai->snapshot_path, "w");
    if (!fp) {
        json_object_put(root);
        return -1;
    }
    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, fp);
    fclose(fp);
    json_object_put(root);
    return written == len ? 0 : -1;
}

static int kolibri_ai_load_snapshot_locked(KolibriAI *ai, struct json_object *root) {
    if (!ai || !root) {
        return -1;
    }

    json_object *value = NULL;
    if (json_object_object_get_ex(root, "iterations", &value) && json_object_is_type(value, json_type_int)) {
        ai->iterations = (uint64_t)json_object_get_int64(value);
    }
    if (json_object_object_get_ex(root, "average_reward", &value) && json_object_is_type(value, json_type_double)) {
        ai->average_reward = json_object_get_double(value);
    }
    if (json_object_object_get_ex(root, "recent_reward", &value) && json_object_is_type(value, json_type_double)) {
        ai->recent_reward = json_object_get_double(value);
    }
    if (json_object_object_get_ex(root, "planning_score", &value) && json_object_is_type(value, json_type_double)) {
        ai->planning_score = json_object_get_double(value);
    }
    if (json_object_object_get_ex(root, "recent_poe", &value) && json_object_is_type(value, json_type_double)) {
        ai->recent_poe = json_object_get_double(value);
    }
    if (json_object_object_get_ex(root, "recent_mdl", &value) && json_object_is_type(value, json_type_double)) {
        ai->recent_mdl = json_object_get_double(value);
    }

    kolibri_ai_dataset_clear(&ai->dataset);
    json_object *dataset = NULL;
    if (json_object_object_get_ex(root, "dataset", &dataset) && json_object_is_type(dataset, json_type_array)) {
        size_t length = json_object_array_length(dataset);
        size_t start = 0;
        if (ai->snapshot_limit > 0 && length > ai->snapshot_limit) {
            start = length - ai->snapshot_limit;
        }
        for (size_t i = start; i < length; ++i) {
            json_object *entry = json_object_array_get_idx(dataset, (int)i);
            if (!entry || !json_object_is_type(entry, json_type_object)) {
                continue;
            }
            KolibriAIDatasetEntry item;
            memset(&item, 0, sizeof(item));
            json_object *field = NULL;
            if (json_object_object_get_ex(entry, "prompt", &field) && json_object_is_type(field, json_type_string)) {
                strncpy(item.prompt, json_object_get_string(field), sizeof(item.prompt) - 1);
            }
            if (json_object_object_get_ex(entry, "response", &field) && json_object_is_type(field, json_type_string)) {
                strncpy(item.response, json_object_get_string(field), sizeof(item.response) - 1);
            }
            if (json_object_object_get_ex(entry, "reward", &field) && json_object_is_type(field, json_type_double)) {
                item.reward = json_object_get_double(field);
            }
            if (json_object_object_get_ex(entry, "poe", &field) && json_object_is_type(field, json_type_double)) {
                item.poe = json_object_get_double(field);
            }
            if (json_object_object_get_ex(entry, "mdl", &field) && json_object_is_type(field, json_type_double)) {
                item.mdl = json_object_get_double(field);
            }
            if (json_object_object_get_ex(entry, "timestamp", &field) &&
                (json_object_is_type(field, json_type_int) || json_object_is_type(field, json_type_int64))) {
                item.timestamp = (time_t)json_object_get_int64(field);
            }
            kolibri_ai_dataset_append(&ai->dataset, &item);
        }
    }

    kolibri_memory_module_clear(&ai->memory);
    json_object *memory = NULL;
    if (json_object_object_get_ex(root, "memory", &memory) && json_object_is_type(memory, json_type_array)) {
        size_t length = json_object_array_length(memory);
        for (size_t i = 0; i < length; ++i) {
            json_object *entry = json_object_array_get_idx(memory, (int)i);
            if (!entry || !json_object_is_type(entry, json_type_object)) {
                continue;
            }
            KolibriMemoryFact fact;
            memset(&fact, 0, sizeof(fact));
            json_object *field = NULL;
            if (json_object_object_get_ex(entry, "key", &field) && json_object_is_type(field, json_type_string)) {
                strncpy(fact.key, json_object_get_string(field), sizeof(fact.key) - 1);
            }
            if (json_object_object_get_ex(entry, "value", &field) && json_object_is_type(field, json_type_string)) {
                strncpy(fact.value, json_object_get_string(field), sizeof(fact.value) - 1);
            }
            if (json_object_object_get_ex(entry, "salience", &field) && json_object_is_type(field, json_type_double)) {
                fact.salience = json_object_get_double(field);
            }
            if (json_object_object_get_ex(entry, "timestamp", &field) &&
                (json_object_is_type(field, json_type_int) || json_object_is_type(field, json_type_int64))) {
                fact.last_updated = (time_t)json_object_get_int64(field);
            } else {
                fact.last_updated = time(NULL);
            }
            kolibri_memory_module_record(&ai->memory, &fact);
        }
    }

    return 0;
}

static int kolibri_ai_import_snapshot_json(KolibriAI *ai, struct json_object *root) {
    if (!ai || !root) {
        return -1;
    }
    pthread_mutex_lock(&ai->mutex);
    int rc = kolibri_ai_load_snapshot_locked(ai, root);
    pthread_mutex_unlock(&ai->mutex);
    return rc;
}

static int kolibri_ai_load_snapshot_file(KolibriAI *ai, const char *path) {
    if (!ai || !path || path[0] == '\0') {
        return 0;
    }
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    char *buffer = malloc((size_t)len + 1);
    if (!buffer) {
        fclose(fp);
        return -1;
    }
    size_t read = fread(buffer, 1, (size_t)len, fp);
    fclose(fp);
    if (read != (size_t)len) {
        free(buffer);
        return -1;
    }
    buffer[len] = '\0';
    int rc = kolibri_ai_import_snapshot(ai, buffer);
    free(buffer);
    return rc;
}

static int kolibri_ai_search_emit(const Formula *formula, void *user_data) {
    formula_search_ctx_t *ctx = user_data;
    if (!ctx || !ctx->ai || !formula) {
        return 0;
    }
    if (ctx->limit > 0 && ctx->added >= ctx->limit) {
        return 1;
    }
    if (formula_collection_add(ctx->ai->library, formula) == 0) {
        ctx->added++;
    }
    if (ctx->limit > 0 && ctx->added >= ctx->limit) {
        return 1;
    }
    return 0;
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

    kolibri_curriculum_init(&ai->curriculum);
    kolibri_ai_dataset_init(&ai->dataset);
    kolibri_memory_module_init(&ai->memory);

    ai->search_config = cfg ? cfg->search : formula_search_config_default();
    if (ai->search_config.max_candidates == 0) {
        ai->search_config.max_candidates = 8;
    }

    ai->selfplay_config.tasks_per_iteration = 8;
    ai->selfplay_config.max_difficulty = 3;
    ai->selfplay_current_difficulty = 1;
    ai->selfplay_total_interactions = 0;
    ai->selfplay_reward_avg = 0.0;
    ai->selfplay_recent_total = 0;
    ai->selfplay_recent_success = 0;

    ai->average_reward = 0.0;
    ai->recent_reward = 0.0;
    ai->exploration_rate = 0.35;
    ai->exploitation_rate = 0.65;
    ai->planning_score = 0.0;
    ai->recent_poe = 0.0;
    ai->recent_mdl = 0.0;
    ai->iterations = 0;

    ai->rng_state = cfg && cfg->seed != 0 ? cfg->seed : (unsigned int)time(NULL);

    if (cfg) {
        strncpy(ai->snapshot_path, cfg->ai.snapshot_path, sizeof(ai->snapshot_path) - 1);
        ai->snapshot_path[sizeof(ai->snapshot_path) - 1] = '\0';
        ai->snapshot_limit = cfg->ai.snapshot_limit;
    } else {
        strncpy(ai->snapshot_path, "data/kolibri_ai_snapshot.json", sizeof(ai->snapshot_path) - 1);
        ai->snapshot_path[sizeof(ai->snapshot_path) - 1] = '\0';
        ai->snapshot_limit = 2048;
    }

    ai->library = formula_collection_create(8);
    if (!ai->library) {
        pthread_mutex_destroy(&ai->mutex);
        free(ai);
        return NULL;
    }

    ai->pipeline = formula_training_pipeline_create(16);
    if (!ai->pipeline) {
        formula_collection_destroy(ai->library);
        pthread_mutex_destroy(&ai->mutex);
        free(ai);
        return NULL;
    }

    kolibri_ai_seed_library(ai);

    if (ai->snapshot_path[0] != '\0') {
        kolibri_ai_load_snapshot_file(ai, ai->snapshot_path);
    }


static void recompute_average_reward(KolibriAI *ai) {
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

KolibriAI *kolibri_ai_create_with_config(const kolibri_config_t *cfg) {
    KolibriAI *ai = calloc(1, sizeof(KolibriAI));
    if (!ai) {
        return NULL;
    }
    ai->library = formula_collection_create(8);
    if (!ai->library) {
        free(ai);
        return NULL;
    }
    ai->average_reward = 0.0;
    ai->exploration_rate = 0.35;
    ai->exploitation_rate = 0.65;
    ai->iterations = 0;
    ai->running = 0;
    pthread_mutex_init(&ai->mutex, NULL);

    if (cfg) {
        kolibri_ai_apply_config(ai, cfg);
    }

    return ai;
}

KolibriAI *kolibri_ai_create(void) {
    return kolibri_ai_create_with_config(NULL);
}

void kolibri_ai_destroy(KolibriAI *ai) {
    if (!ai) {
        return;
    }


    kolibri_ai_stop(ai);
    kolibri_ai_dataset_clear(&ai->dataset);
    kolibri_memory_module_clear(&ai->memory);
    if (ai->library) {
        formula_collection_destroy(ai->library);
    }
    if (ai->pipeline) {
        formula_training_pipeline_destroy(ai->pipeline);
    }
    pthread_mutex_destroy(&ai->mutex);
    free(ai);
}

static void *kolibri_ai_worker(void *arg) {
    KolibriAI *ai = arg;
    while (1) {
        pthread_mutex_lock(&ai->mutex);
        int running = ai->running;
        pthread_mutex_unlock(&ai->mutex);
        if (!running) {
            break;
        }
        kolibri_ai_process_iteration(ai);
        struct timespec ts = {0, 120 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    return NULL;
}


    pthread_mutex_destroy(&ai->mutex);
    if (ai->library) {
        formula_collection_destroy(ai->library);
    }
    free(ai);
}


void kolibri_ai_start(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    ai->running = 1;
    pthread_mutex_unlock(&ai->mutex);

    pthread_create(&ai->worker, NULL, kolibri_ai_worker, ai);

}

void kolibri_ai_stop(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    ai->running = 0;
    pthread_mutex_unlock(&ai->mutex);

    if (was_running) {
        pthread_join(ai->worker, NULL);
    }
    if (kolibri_ai_persist(ai) != 0) {
        log_warn("kolibri_ai: failed to persist snapshot to %s", ai->snapshot_path);
    }

}

void kolibri_ai_process_iteration(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);
    ai->iterations++;

    double phase = (double)ai->iterations / 24.0;
    double oscillation = 0.05 * sin(phase * M_PI * 2.0);
    ai->exploration_rate = clamp01(ai->exploration_rate + oscillation);
    ai->exploitation_rate = clamp01(1.0 - ai->exploration_rate);
    pthread_mutex_unlock(&ai->mutex);

}

void kolibri_ai_set_selfplay_config(KolibriAI *ai, const KolibriAISelfplayConfig *config) {
    if (!ai || !config) {
        return;
    }
    pthread_mutex_lock(&ai->mutex);

    ai->selfplay_config = *config;
    if (ai->selfplay_config.tasks_per_iteration == 0) {
        ai->selfplay_config.tasks_per_iteration = 1;
    }
    if (ai->selfplay_config.max_difficulty == 0) {
        ai->selfplay_config.max_difficulty = 1;
    }
    if (ai->selfplay_current_difficulty == 0) {
        ai->selfplay_current_difficulty = 1;
    }
    if (ai->selfplay_current_difficulty > ai->selfplay_config.max_difficulty) {
        ai->selfplay_current_difficulty = ai->selfplay_config.max_difficulty;
    }

    double factor = config->tasks_per_iteration > 0 ? (double)config->tasks_per_iteration : 1.0;
    double normalised = clamp01(factor / 16.0);
    ai->exploration_rate = clamp01(0.25 + normalised * 0.5);
    ai->exploitation_rate = clamp01(1.0 - ai->exploration_rate);

    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_record_interaction(KolibriAI *ai, const KolibriAISelfplayInteraction *interaction) {
    if (!ai || !interaction) {
        return;
    }


    KolibriAIDatasetEntry entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.prompt, sizeof(entry.prompt), "%s", interaction->task.description);
    snprintf(entry.response, sizeof(entry.response),
             "pred=%.3f expected=%.3f",
             interaction->predicted_result,
             interaction->task.expected_result);
    entry.reward = interaction->reward;
    entry.poe = kolibri_clamp(1.0 - fabs(interaction->error), 0.0, 1.0);
    entry.mdl = (double)interaction->task.difficulty;
    entry.timestamp = time(NULL);

    KolibriMemoryFact fact;
    memset(&fact, 0, sizeof(fact));
    snprintf(fact.key, sizeof(fact.key), "%s", interaction->task.description);
    snprintf(fact.value, sizeof(fact.value),
             "%.3f -> %.3f (%s)",
             interaction->task.expected_result,
             interaction->predicted_result,
             interaction->success ? "ok" : "err");
    fact.salience = interaction->reward;
    fact.last_updated = entry.timestamp;

    pthread_mutex_lock(&ai->mutex);
    kolibri_ai_dataset_append(&ai->dataset, &entry);
    kolibri_ai_dataset_trim(&ai->dataset, ai->snapshot_limit);
    kolibri_memory_module_record(&ai->memory, &fact);

    ai->selfplay_total_interactions++;
    double count = (double)ai->selfplay_total_interactions;
    ai->selfplay_reward_avg += (interaction->reward - ai->selfplay_reward_avg) / fmax(count, 1.0);

    ai->selfplay_recent_total++;
    if (interaction->success) {
        ai->selfplay_recent_success++;
    }
    size_t window = ai->selfplay_config.tasks_per_iteration;
    if (window == 0) {
        window = 1;
    }
    if (ai->selfplay_recent_total >= window) {
        double ratio = ai->selfplay_recent_total > 0
                            ? (double)ai->selfplay_recent_success / (double)ai->selfplay_recent_total
                            : 0.0;
        if (ratio > 0.75 && ai->selfplay_current_difficulty < ai->selfplay_config.max_difficulty) {
            ai->selfplay_current_difficulty++;
        } else if (ratio < 0.35 && ai->selfplay_current_difficulty > 1) {
            ai->selfplay_current_difficulty--;
        }
        ai->selfplay_recent_total = 0;
        ai->selfplay_recent_success = 0;
    }
    pthread_mutex_unlock(&ai->mutex);

    log_info("self-play: %s | pred=%.3f expected=%.3f reward=%.3f success=%s diff=%u",
             interaction->task.description,
             interaction->predicted_result,
             interaction->task.expected_result,
             interaction->reward,
             interaction->success ? "yes" : "no",
             interaction->task.difficulty);

    pthread_mutex_lock(&ai->mutex);
    double delta = interaction->reward * 0.1;
    ai->average_reward = clamp01(ai->average_reward + delta);
    pthread_mutex_unlock(&ai->mutex);

}

void kolibri_ai_apply_config(KolibriAI *ai, const kolibri_config_t *cfg) {
    if (!ai || !cfg) {
        return;
    }

    pthread_mutex_lock(&ai->mutex);
    ai->search_config = cfg->search;
    if (cfg->seed != 0) {
        ai->rng_state = cfg->seed;

    KolibriAISelfplayConfig sp = {
        .tasks_per_iteration = cfg->selfplay.tasks_per_iteration,
        .max_difficulty = cfg->selfplay.max_difficulty,
    };
    kolibri_ai_set_selfplay_config(ai, &sp);
    pthread_mutex_lock(&ai->mutex);
    if (cfg->search.base_effectiveness > 0.0) {
        ai->average_reward = clamp01(cfg->search.base_effectiveness);

    }
    strncpy(ai->snapshot_path, cfg->ai.snapshot_path, sizeof(ai->snapshot_path) - 1);
    ai->snapshot_path[sizeof(ai->snapshot_path) - 1] = '\0';
    ai->snapshot_limit = cfg->ai.snapshot_limit;
    pthread_mutex_unlock(&ai->mutex);
}

KolibriDifficultyLevel kolibri_ai_plan_actions(KolibriAI *ai, double *expected_reward) {
    if (!ai) {
        return KOLIBRI_DIFFICULTY_FOUNDATION;
    }
    pthread_mutex_lock(&ai->mutex);
    double sample = kolibri_ai_rand_uniform(ai);
    double cumulative = 0.0;
    KolibriDifficultyLevel level = KOLIBRI_DIFFICULTY_FOUNDATION;
    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
        cumulative += ai->curriculum.distribution[i];
        if (sample <= cumulative) {
            level = (KolibriDifficultyLevel)i;
            break;
        }
    }
    double baseline = ai->curriculum.reward_ema[level];
    if (baseline <= 0.0) {
        baseline = ai->average_reward > 0.0 ? ai->average_reward : 0.5;
    }
    if (expected_reward) {
        *expected_reward = baseline;
    }
    ai->curriculum.current_level = level;
    pthread_mutex_unlock(&ai->mutex);
    return level;
}

KolibriDifficultyLevel kolibri_ai_plan_actions(KolibriAI *ai, double *expected_reward) {
    if (!ai) {
        if (expected_reward) {
            *expected_reward = 0.0;
        }
        return KOLIBRI_DIFFICULTY_FOUNDATION;
    }
    pthread_mutex_lock(&ai->mutex);

    ai->iterations++;

    double jitter = (kolibri_ai_rand_uniform(ai) - 0.5) * 0.1;
    double reward = kolibri_clamp(expected_reward + jitter, 0.0, 1.0);
    ai->recent_reward = reward;

    if (ai->average_reward <= 0.0) {
        ai->average_reward = reward;
    } else {
        ai->average_reward = 0.9 * ai->average_reward + 0.1 * reward;
    }

    double target_success = 0.6;
    double success_value = reward >= (0.45 + 0.12 * (double)level) ? 1.0 : 0.0;
    double alpha = ai->curriculum.ema_alpha;
    ai->curriculum.reward_ema[level] =
        (1.0 - alpha) * ai->curriculum.reward_ema[level] + alpha * reward;
    ai->curriculum.success_ema[level] =
        (1.0 - alpha) * ai->curriculum.success_ema[level] + alpha * success_value;
    ai->curriculum.sample_count[level]++;

    ai->curriculum.global_success_ema =
        (1.0 - alpha) * ai->curriculum.global_success_ema + alpha * success_value;
    double error = target_success - ai->curriculum.global_success_ema;
    ai->curriculum.integral_error += error;
    ai->curriculum.last_error = error;

    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
        double adjust = (ai->curriculum.success_ema[i] - target_success) * 0.05;
        ai->curriculum.distribution[i] =
            kolibri_clamp(ai->curriculum.distribution[i] - adjust, 0.05, 1.0);
    }
    kolibri_curriculum_normalize(ai->curriculum.distribution, KOLIBRI_DIFFICULTY_COUNT);

    double exploitation_target = 0.5 + 0.1 * ai->curriculum.current_level;
    ai->exploitation_rate =
        kolibri_clamp(0.9 * ai->exploitation_rate + 0.1 * exploitation_target, 0.45, 0.9);
    ai->exploration_rate = kolibri_clamp(1.0 - ai->exploitation_rate, 0.1, 0.55);

    if (ai->iterations % 48 == 0) {
        kolibri_ai_synthesise_formula_locked(ai);
    }
    pthread_mutex_unlock(&ai->mutex);

    double reward = ai->average_reward;
    pthread_mutex_unlock(&ai->mutex);
    if (expected_reward) {
        *expected_reward = reward;
    }
    if (reward < 0.33) {
        return KOLIBRI_DIFFICULTY_FOUNDATION;
    }
    if (reward < 0.66) {
        return KOLIBRI_DIFFICULTY_SKILLS;
    }
    if (reward < 0.85) {
        return KOLIBRI_DIFFICULTY_ADVANCED;
    }
    return KOLIBRI_DIFFICULTY_CHALLENGE;

}

int kolibri_ai_apply_reinforcement(KolibriAI *ai,
                                   KolibriDifficultyLevel level,
                                   double reward,
                                   int success) {
    (void)level;
    (void)success;
    if (!ai) {
        return -1;
    }
    pthread_mutex_lock(&ai->mutex);

    int rc = formula_collection_add(ai->library, formula);
    if (rc == 0) {
        kolibri_ai_recompute_average_locked(ai);
    }

    double blend = 0.2;
    ai->average_reward = clamp01(ai->average_reward * (1.0 - blend) + reward * blend);

    pthread_mutex_unlock(&ai->mutex);
    return 0;
}

int kolibri_ai_add_formula(KolibriAI *ai, const Formula *formula) {
    if (!ai || !formula) {
        return -1;
    }


    double reward = kolibri_clamp(experience->reward, 0.0, 1.0);
    double poe = kolibri_clamp(experience->poe, 0.0, 1.0);
    double mdl = experience->mdl < 0.0 ? 0.0 : experience->mdl;
    double planning_update = poe - 0.25 * mdl;
    if (planning_update < 0.0) {
        planning_update = 0.0;
    }

    pthread_mutex_lock(&ai->mutex);
    ai->recent_poe = poe;
    ai->recent_mdl = mdl;
    ai->recent_reward = reward;
    ai->average_reward = 0.85 * ai->average_reward + 0.15 * reward;
    if (ai->planning_score <= 0.0) {
        ai->planning_score = planning_update;
    } else {
        ai->planning_score = 0.75 * ai->planning_score + 0.25 * planning_update;
    }

    if (ai->library && formula) {
        Formula *existing = formula_collection_find(ai->library, formula->id);
        if (existing) {
            existing->effectiveness = kolibri_clamp(0.6 * existing->effectiveness + 0.4 * reward, 0.0, 1.0);
            existing->confirmations += 1;
            existing->tests_passed += 1;
        } else if (poe >= 0.55) {
            Formula copy;
            memset(&copy, 0, sizeof(copy));
            copy = *formula;
            copy.effectiveness = reward;
            copy.created_at = time(NULL);
            formula_collection_add(ai->library, &copy);
        }
        kolibri_ai_recompute_average_locked(ai);

    pthread_mutex_lock(&ai->mutex);
    int rc = formula_collection_add(ai->library, formula);
    if (rc == 0) {
        recompute_average_reward(ai);

    }
    pthread_mutex_unlock(&ai->mutex);
    return rc;
}

Formula *kolibri_ai_get_best_formula(KolibriAI *ai) {
    if (!ai || !ai->library || ai->library->count == 0) {
        return NULL;
    }

    const Formula *top[1] = {0};
    pthread_mutex_lock(&ai->mutex);
    size_t count = formula_collection_get_top(ai->library, top, 1);
    Formula *result = NULL;
    if (count == 1 && top[0]) {
        result = (Formula *)top[0];

    pthread_mutex_lock(&ai->mutex);
    size_t best_index = 0;
    double best_score = ai->library->formulas[0].effectiveness;
    for (size_t i = 1; i < ai->library->count; ++i) {
        double score = ai->library->formulas[i].effectiveness;
        if (score > best_score) {
            best_score = score;
            best_index = i;
        }

    }
    Formula *result = &ai->library->formulas[best_index];
    pthread_mutex_unlock(&ai->mutex);
    return result;

}

static char *kolibri_ai_alloc_json(size_t initial) {
    char *buf = malloc(initial);
    if (buf) {
        buf[0] = '\0';
    }
    return buf;

}

char *kolibri_ai_serialize_state(const KolibriAI *ai) {
    if (!ai) {
        return NULL;
    }
    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);

    uint64_t iterations = ai->iterations;
    size_t formula_count = ai->library ? ai->library->count : 0;
    double avg_reward = ai->average_reward;
    double recent_reward = ai->recent_reward;
    double exploitation = ai->exploitation_rate;

    double average = ai->average_reward;
    size_t count = ai->library ? ai->library->count : 0;
    size_t iterations = ai->iterations;

    double exploration = ai->exploration_rate;
    double exploitation = ai->exploitation_rate;
    int running = ai->running;
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);


    char temp[512];
    int written = snprintf(temp,
                           sizeof(temp),
                           "{\"iterations\":%llu,\"formula_count\":%zu,\"average_reward\":%.6f,"
                           "\"recent_reward\":%.6f,\"planning_score\":%.6f,\"recent_poe\":%.6f,"
                           "\"recent_mdl\":%.6f,\"exploitation_rate\":%.6f,\"exploration_rate\":%.6f,\"running\":%d}",
                           (unsigned long long)iterations,
                           formula_count,
                           avg_reward,
                           recent_reward,
                           planning,
                           recent_poe,
                           recent_mdl,
                           exploitation,
                           exploration,

    char buffer[256];
    double planning = clamp01(average * 0.9 + exploitation * 0.1);
    double recent_poe = clamp01(average + 0.1);
    double recent_mdl = clamp01(1.0 - average);

    int written = snprintf(buffer,
                           sizeof(buffer),
                           "{\"average_reward\":%.6f,\"formula_count\":%zu,"
                           "\"iterations\":%zu,\"exploration_rate\":%.6f,"
                           "\"exploitation_rate\":%.6f,\"planning_score\":%.6f,"
                           "\"recent_poe\":%.6f,\"recent_mdl\":%.6f,\"running\":%d}",
                           average,
                           count,
                           iterations,
                           exploration,
                           exploitation,
                           planning,
                           recent_poe,
                           recent_mdl,

                           running);
    if (written < 0) {
        return NULL;
    }
    size_t needed = (size_t)written + 1;
    char *json = malloc(needed);
    if (!json) {
        return NULL;
    }
    memcpy(json, buffer, needed);
    return json;
}

char *kolibri_ai_serialize_formulas(const KolibriAI *ai, size_t max_results) {
    if (!ai) {
        return NULL;
    }
    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    size_t count = ai->library ? ai->library->count : 0;
    size_t limit = max_results && max_results < count ? max_results : count;
    Formula const **top = NULL;
    if (limit > 0 && ai->library) {
        top = calloc(limit, sizeof(*top));
        if (top) {
            formula_collection_get_top(ai->library, top, limit);
        }
    }
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    size_t capacity = 160 + limit * 160;
    char *json = malloc(capacity);
    if (!json) {
        free(top);
        return NULL;
    }
    size_t offset = 0;
    offset += (size_t)snprintf(json + offset, capacity - offset, "{\"count\":%zu,\"formulas\":[", limit);
    for (size_t i = 0; top && i < limit; ++i) {
        if (i > 0) {
            offset += (size_t)snprintf(json + offset, capacity - offset, ",");
        }
        const Formula *formula = top[i];
        offset += (size_t)snprintf(json + offset,
                                   capacity - offset,
                                   "{\"id\":\"%s\",\"effectiveness\":%.6f}",
                                   formula ? formula->id : "",
                                   formula ? formula->effectiveness : 0.0);
    }
    offset += (size_t)snprintf(json + offset, capacity - offset, "]}");
    free(top);
    return json;
}


char *kolibri_ai_export_snapshot(const KolibriAI *ai) {
    if (!ai) {
        return NULL;
    }
    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    json_object *root = kolibri_ai_build_snapshot_locked(ai);
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
    if (!root) {
        return NULL;
    }
    const char *json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    if (!json_str) {
        json_object_put(root);
        return NULL;
    }
    char *copy = strdup(json_str);
    json_object_put(root);
    return copy;
}

int kolibri_ai_import_snapshot(KolibriAI *ai, const char *json) {
    if (!ai || !json) {
        return -1;
    }
    struct json_tokener *tok = json_tokener_new();
    if (!tok) {
        return -1;
    }
    struct json_object *root = json_tokener_parse_ex(tok, json, (int)strlen(json));
    enum json_tokener_error jerr = json_tokener_get_error(tok);
    json_tokener_free(tok);
    if (jerr != json_tokener_success || !root) {
        if (root) {
            json_object_put(root);
        }
        return -1;
    }
    if (!json_object_is_type(root, json_type_object)) {
        json_object_put(root);
        return -1;
    }
    int rc = kolibri_ai_import_snapshot_json(ai, root);
    json_object_put(root);
    return rc;
}

int kolibri_ai_sync_with_neighbor(KolibriAI *ai, const char *base_url) {
    (void)ai;
    (void)base_url;
    log_info("kolibri_ai: neighbor sync requested (stub implementation)");
    return 0;
}


