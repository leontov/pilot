/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "kolibri_ai.h"

#include "util/log.h"

#include <json-c/json.h>

#include <ctype.h>
#include <errno.h>

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
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

static int kolibri_ai_persist(KolibriAI *ai);

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

static char *duplicate_cstring(const char *src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
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

static void memory_trim(KolibriMemoryModule *memory, size_t limit) {
    if (!memory || limit == 0 || memory->count <= limit) {
        return;
    }
    size_t offset = memory->count - limit;
    memmove(memory->facts,
            memory->facts + offset,
            limit * sizeof(memory->facts[0]));
    memory->count = limit;
}

static unsigned int prng_next(KolibriAI *ai) {
    ai->rng_state = ai->rng_state * 1664525u + 1013904223u;
    return ai->rng_state;
}

static void kolibri_ai_dataset_clear(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    dataset_clear(&ai->dataset);
    ai->selfplay_total_interactions = 0;
}

static void kolibri_memory_module_clear(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    memory_clear(&ai->memory);
}

static int ensure_parent_directory(const char *path) {
    if (!path || path[0] == '\0') {
        return 0;
    }

    char buffer[512];
    size_t len = strnlen(path, sizeof(buffer));
    if (len >= sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    memcpy(buffer, path, len);
    buffer[len] = '\0';

    char *last_slash = strrchr(buffer, '/');
    if (!last_slash) {
        return 0;
    }

    *last_slash = '\0';
    if (buffer[0] == '\0') {
        return 0;
    }

    char tmp[512];
    size_t pos = 0;
    while (buffer[pos] != '\0') {
        while (buffer[pos] == '/') {
            pos++;
        }
        if (buffer[pos] == '\0') {
            break;
        }
        size_t start = pos;
        while (buffer[pos] != '\0' && buffer[pos] != '/') {
            pos++;
        }
        size_t segment_len = pos - start;
        if (segment_len == 0) {
            continue;
        }
        if (start + segment_len >= sizeof(tmp)) {
            return -1;
        }
        memcpy(tmp, buffer, start + segment_len);
        tmp[start + segment_len] = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
    }
    return 0;
}

static void json_escape(FILE *fp, const char *str) {
    if (!fp || !str) {
        return;
    }
    for (const unsigned char *p = (const unsigned char *)str; *p; ++p) {
        switch (*p) {
        case '\\':
        case '"':
            fputc('\\', fp);
            fputc((int)*p, fp);
            break;
        case '\n':
            fputs("\\n", fp);
            break;
        case '\r':
            fputs("\\r", fp);
            break;
        case '\t':
            fputs("\\t", fp);
            break;
        default:
            if (*p < 0x20) {
                fprintf(fp, "\\u%04x", *p);
            } else {
                fputc((int)*p, fp);
            }
            break;
        }
    }
}

static int kolibri_ai_dump_snapshot_locked(KolibriAI *ai, FILE *fp) {
    if (!ai || !fp) {
        return -1;
    }

    fprintf(fp,
            "{\"iterations\":%llu,\"average_reward\":%.6f,"
            "\"planning_score\":%.6f,\"recent_poe\":%.6f,"
            "\"recent_mdl\":%.6f,\"selfplay_total_interactions\":%llu,",
            (unsigned long long)ai->iterations,
            ai->average_reward,
            ai->planning_score,
            ai->recent_poe,
            ai->recent_mdl,
            (unsigned long long)ai->selfplay_total_interactions);

    fputs("\"formulas\":[", fp);
    for (size_t i = 0; ai->library && i < ai->library->count; ++i) {
        const Formula *formula = &ai->library->formulas[i];
        if (i > 0) {
            fputc(',', fp);
        }
        fputs("{\"id\":\"", fp);
        json_escape(fp, formula->id);
        fprintf(fp,
                "\",\"effectiveness\":%.6f,\"created_at\":%lld,"
                "\"tests_passed\":%u,\"confirmations\":%u}",
                formula->effectiveness,
                (long long)formula->created_at,
                formula->tests_passed,
                formula->confirmations);
    }
    fputs("],", fp);

    fputs("\"dataset\":[", fp);
    for (size_t i = 0; i < ai->dataset.count; ++i) {
        const KolibriAIDatasetEntry *entry = &ai->dataset.entries[i];
        if (i > 0) {
            fputc(',', fp);
        }
        fputs("{\"prompt\":\"", fp);
        json_escape(fp, entry->prompt);
        fputs("\",\"response\":\"", fp);
        json_escape(fp, entry->response);
        fprintf(fp,
                "\",\"reward\":%.6f,\"poe\":%.6f,\"mdl\":%.6f,\"timestamp\":%lld}",
                entry->reward,
                entry->poe,
                entry->mdl,
                (long long)entry->timestamp);
    }
    fputs("],", fp);

    fputs("\"memory\":[", fp);
    for (size_t i = 0; i < ai->memory.count; ++i) {
        const KolibriMemoryFact *fact = &ai->memory.facts[i];
        if (i > 0) {
            fputc(',', fp);
        }
        fputs("{\"key\":\"", fp);
        json_escape(fp, fact->key);
        fputs("\",\"value\":\"", fp);
        json_escape(fp, fact->value);
        fprintf(fp,
                "\",\"salience\":%.6f,\"last_updated\":%lld}",
                fact->salience,
                (long long)fact->last_updated);
    }
    fputs("]}", fp);
    return 0;
}

static int kolibri_ai_persist(KolibriAI *ai) {
    if (!ai || ai->snapshot_path[0] == '\0') {
        return 0;
    }

    if (ensure_parent_directory(ai->snapshot_path) != 0) {
        log_warn("Не удалось создать каталоги для снапшота: %s", ai->snapshot_path);
        return -1;
    }

    FILE *fp = fopen(ai->snapshot_path, "w");
    if (!fp) {
        log_warn("Не удалось открыть файл снапшота: %s (%s)",
                 ai->snapshot_path,
                 strerror(errno));
        return -1;
    }

    pthread_mutex_lock(&ai->mutex);
    int rc = kolibri_ai_dump_snapshot_locked(ai, fp);
    pthread_mutex_unlock(&ai->mutex);

    if (rc != 0) {
        fclose(fp);
        return -1;
    }

    if (fflush(fp) != 0) {
        log_warn("Не удалось сбросить снапшот на диск: %s", strerror(errno));
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static int read_file_into_buffer(const char *path, char **out_buffer) {
    if (!path || !out_buffer) {
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (errno == ENOENT) {
            return 1;
        }
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }

    char *buffer = calloc((size_t)size + 1, sizeof(char));
    if (!buffer) {
        fclose(fp);
        return -1;
    }

    size_t read = fread(buffer, 1, (size_t)size, fp);
    fclose(fp);
    if (read != (size_t)size) {
        free(buffer);
        return -1;
    }

    buffer[size] = '\0';
    *out_buffer = buffer;
    return 0;
}

static int kolibri_ai_load_snapshot(KolibriAI *ai) {
    if (!ai || ai->snapshot_path[0] == '\0') {
        return 0;
    }

    char *buffer = NULL;
    int rc = read_file_into_buffer(ai->snapshot_path, &buffer);
    if (rc == 1) {
        return 0;
    }
    if (rc != 0) {
        log_warn("Не удалось прочитать снапшот: %s", ai->snapshot_path);
        return -1;
    }

    int import_rc = kolibri_ai_import_snapshot(ai, buffer);
    free(buffer);
    if (import_rc != 0) {
        log_warn("Снапшот повреждён и будет проигнорирован: %s", ai->snapshot_path);
        return -1;
    }
    return 0;
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
    kolibri_ai_load_snapshot(ai);
    if (!ai->library || ai->library->count == 0) {
        seed_library(ai);
    }
    update_average_reward(ai);
    return ai;
}

void kolibri_ai_destroy(KolibriAI *ai) {
    if (!ai) {
        return;
    }
    kolibri_ai_stop(ai);
    kolibri_ai_persist(ai);
    kolibri_ai_dataset_clear(ai);
    kolibri_memory_module_clear(ai);
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
    kolibri_ai_persist(ai);
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
    if (ai->snapshot_limit > 0) {
        dataset_trim(&ai->dataset, ai->snapshot_limit);
        memory_trim(&ai->memory, ai->snapshot_limit);
    }
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
    Formula *copy = NULL;
    if (best) {
        copy = calloc(1, sizeof(*copy));
        if (copy) {
            if (formula_copy(copy, best) != 0) {
                free(copy);
                copy = NULL;
            }
        }
    }
    pthread_mutex_unlock(&ai->mutex);
    return copy;
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
    struct json_object *formulas = json_object_new_array();
    struct json_object *dataset = json_object_new_array();
    struct json_object *memory = json_object_new_array();
    if (!root || !formulas || !dataset || !memory) {
        if (formulas) {
            json_object_put(formulas);
        }
        if (dataset) {
            json_object_put(dataset);
        }
        if (memory) {
            json_object_put(memory);
        }
        if (root) {
            json_object_put(root);
        }


    size_t formulas_cap = 2;
    if (ai->library) {
        formulas_cap += ai->library->count * 160;
    }
    size_t dataset_cap = 2 + ai->dataset.count * 512;
    size_t memory_cap = 2 + ai->memory.count * 256;
    size_t total = 256 + formulas_cap + dataset_cap + memory_cap;
    char *buffer = malloc(total);
    if (!buffer) {

        pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
        return NULL;
    }


    json_object_object_add(root, "iterations", json_object_new_int64((long long)ai->iterations));
    json_object_object_add(root, "average_reward", json_object_new_double(ai->average_reward));
    json_object_object_add(root, "planning_score", json_object_new_double(ai->planning_score));
    json_object_object_add(root, "recent_poe", json_object_new_double(ai->recent_poe));
    json_object_object_add(root, "recent_mdl", json_object_new_double(ai->recent_mdl));

    if (ai->library) {
        for (size_t i = 0; i < ai->library->count; ++i) {
            const Formula *formula = &ai->library->formulas[i];
            struct json_object *entry = json_object_new_object();
            if (!entry) {
                json_object_put(root);
                pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
                return NULL;
            }
            json_object_object_add(entry, "id", json_object_new_string(formula->id));
            json_object_object_add(entry, "effectiveness", json_object_new_double(formula->effectiveness));
            json_object_array_add(formulas, entry);
        }
    }
    json_object_object_add(root, "formulas", formulas);

    size_t dataset_count = ai->dataset.count;
    size_t dataset_limit = ai->snapshot_limit ? ai->snapshot_limit : dataset_count;
    size_t dataset_start = dataset_count > dataset_limit ? dataset_count - dataset_limit : 0;
    for (size_t i = dataset_start; i < dataset_count; ++i) {
        const KolibriAIDatasetEntry *entry = &ai->dataset.entries[i];
        struct json_object *obj = json_object_new_object();
        if (!obj) {
            json_object_put(root);
            pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
            return NULL;
        }
        json_object_object_add(obj, "prompt", json_object_new_string(entry->prompt));
        json_object_object_add(obj, "response", json_object_new_string(entry->response));
        json_object_object_add(obj, "reward", json_object_new_double(entry->reward));
        json_object_object_add(obj, "poe", json_object_new_double(entry->poe));
        json_object_object_add(obj, "mdl", json_object_new_double(entry->mdl));
        json_object_object_add(obj, "timestamp", json_object_new_int64((long long)entry->timestamp));
        json_object_array_add(dataset, obj);
    }
    json_object_object_add(root, "dataset", dataset);

    size_t memory_count = ai->memory.count;
    size_t memory_limit = ai->snapshot_limit ? ai->snapshot_limit : memory_count;
    size_t memory_start = memory_count > memory_limit ? memory_count - memory_limit : 0;
    for (size_t i = memory_start; i < memory_count; ++i) {
        const KolibriMemoryFact *fact = &ai->memory.facts[i];
        struct json_object *obj = json_object_new_object();
        if (!obj) {
            json_object_put(root);
            pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
            return NULL;
        }
        json_object_object_add(obj, "key", json_object_new_string(fact->key));
        json_object_object_add(obj, "value", json_object_new_string(fact->value));
        json_object_object_add(obj, "salience", json_object_new_double(fact->salience));
        json_object_object_add(obj, "last_updated", json_object_new_int64((long long)fact->last_updated));
        json_object_array_add(memory, obj);

    FILE *mem = fmemopen(buffer, total, "w");
    if (!mem) {
        pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
        free(buffer);
        return NULL;

    }
    json_object_object_add(root, "memory", memory);


    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    const char *json_text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    char *result = json_text ? duplicate_cstring(json_text) : NULL;
    json_object_put(root);
    return result;

    if (kolibri_ai_dump_snapshot_locked((KolibriAI *)ai, mem) != 0) {
        fclose(mem);
        pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
        free(buffer);
        return NULL;
    }
    fflush(mem);
    long written = ftell(mem);
    fclose(mem);
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    if (written < 0) {
        free(buffer);
        return NULL;
    }
    if ((size_t)written < total) {
        buffer[written] = '\0';
    } else {
        buffer[total - 1] = '\0';
    }
    return buffer;

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
    if (!root || err != json_tokener_success || !json_object_is_type(root, json_type_object)) {
        if (root) {
            json_object_put(root);
        }

    *out = (uint64_t)value;
    return 0;
}

static const char *skip_whitespace_ptr(const char *ptr) {
    while (ptr && *ptr && isspace((unsigned char)*ptr)) {
        ++ptr;
    }
    return ptr;
}

static int parse_json_string_value(const char *start,
                                   const char *limit,
                                   char *out,
                                   size_t out_size,
                                   const char **out_end) {
    if (!start || !out || out_size == 0) {
        return -1;
    }
    if (*start != '"') {
        return -1;
    }
    start++;
    size_t idx = 0;
    while (start < limit && *start && *start != '"') {
        char c = *start;
        if (c == '\\') {
            start++;
            if (start >= limit || *start == '\0') {
                return -1;
            }
            switch (*start) {
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case '"':
            case '\\':
            case '/':
                c = *start;
                break;
            default:
                c = *start;
                break;
            }
        }
        if (idx + 1 < out_size) {
            out[idx++] = c;
        }
        start++;
    }
    if (start >= limit || *start != '"') {
        return -1;
    }
    out[idx < out_size ? idx : out_size - 1] = '\0';
    if (out_end) {
        *out_end = start + 1;
    }
    return 0;
}

static int parse_string_field_limited(const char *json,
                                      const char *limit,
                                      const char *key,
                                      char *out,
                                      size_t out_size) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *pos = strstr(json, pattern);
    if (!pos || (limit && pos >= limit)) {
        return -1;
    }
    pos += strlen(pattern);
    pos = skip_whitespace_ptr(pos);
    if (!pos || (limit && pos >= limit)) {
        return -1;
    }
    const char *end = limit ? limit : json + strlen(json);
    return parse_json_string_value(pos, end, out, out_size, NULL);
}

static int parse_number_field_limited(const char *json,
                                      const char *limit,
                                      const char *key,
                                      double *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *pos = strstr(json, pattern);
    if (!pos || (limit && pos >= limit)) {
        return -1;
    }
    pos += strlen(pattern);
    pos = skip_whitespace_ptr(pos);
    if (!pos || (limit && pos >= limit)) {
        return -1;
    }
    char *endptr = NULL;
    double value = strtod(pos, &endptr);
    if (pos == endptr || (limit && endptr > limit)) {
        return -1;
    }
    if (out) {
        *out = value;
    }
    return 0;
}

static int parse_time_field_limited(const char *json,
                                    const char *limit,
                                    const char *key,
                                    time_t *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *pos = strstr(json, pattern);
    if (!pos || (limit && pos >= limit)) {
        return -1;
    }
    pos += strlen(pattern);
    pos = skip_whitespace_ptr(pos);
    if (!pos || (limit && pos >= limit)) {
        return -1;
    }
    char *endptr = NULL;
    long long value = strtoll(pos, &endptr, 10);
    if (pos == endptr || (limit && endptr > limit)) {
        return -1;
    }
    if (out) {
        *out = (time_t)value;
    }
    return 0;
}

static void parse_formulas_from_snapshot(KolibriAI *ai,
                                         const char *array,
                                         const char *array_end) {
    if (!ai || !array || !array_end) {
        return;
    }

    for (size_t i = 0; i < ai->library->count; ++i) {
        formula_clear(&ai->library->formulas[i]);
    }
    ai->library->count = 0;

    const char *cursor = array;
    while (cursor && cursor < array_end) {
        const char *obj_start = strchr(cursor, '{');
        if (!obj_start || obj_start >= array_end) {
            break;
        }
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end || obj_end > array_end) {
            break;
        }
        Formula formula = {0};
        formula.representation = FORMULA_REPRESENTATION_TEXT;
        parse_string_field_limited(obj_start, obj_end, "id", formula.id, sizeof(formula.id));
        double eff = 0.0;
        if (parse_number_field_limited(obj_start, obj_end, "effectiveness", &eff) == 0) {
            formula.effectiveness = eff;
        }
        time_t created = 0;
        if (parse_time_field_limited(obj_start, obj_end, "created_at", &created) == 0) {
            formula.created_at = created;
        }
        double tests = 0.0;
        if (parse_number_field_limited(obj_start, obj_end, "tests_passed", &tests) == 0) {
            formula.tests_passed = (uint32_t)tests;
        }
        double confirmations = 0.0;
        if (parse_number_field_limited(obj_start, obj_end, "confirmations", &confirmations) == 0) {
            formula.confirmations = (uint32_t)confirmations;
        }
        formula_collection_add(ai->library, &formula);
        cursor = obj_end + 1;
    }
    update_average_reward(ai);
}

static void parse_dataset_from_snapshot(KolibriAI *ai,
                                        const char *array,
                                        const char *array_end) {
    if (!ai || !array || !array_end) {
        return;
    }

    dataset_clear(&ai->dataset);
    const char *cursor = array;
    while (cursor && cursor < array_end) {
        const char *obj_start = strchr(cursor, '{');
        if (!obj_start || obj_start >= array_end) {
            break;
        }
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end || obj_end > array_end) {
            break;
        }

        KolibriAIDatasetEntry entry = {0};
        parse_string_field_limited(obj_start, obj_end, "prompt", entry.prompt, sizeof(entry.prompt));
        parse_string_field_limited(obj_start, obj_end, "response", entry.response, sizeof(entry.response));
        double reward = 0.0;
        if (parse_number_field_limited(obj_start, obj_end, "reward", &reward) == 0) {
            entry.reward = reward;
        }
        double poe = 0.0;
        if (parse_number_field_limited(obj_start, obj_end, "poe", &poe) == 0) {
            entry.poe = poe;
        }
        double mdl = 0.0;
        if (parse_number_field_limited(obj_start, obj_end, "mdl", &mdl) == 0) {
            entry.mdl = mdl;
        }
        time_t ts = 0;
        if (parse_time_field_limited(obj_start, obj_end, "timestamp", &ts) == 0) {
            entry.timestamp = ts;
        }
        dataset_append(&ai->dataset, &entry);
        if (ai->snapshot_limit > 0) {
            dataset_trim(&ai->dataset, ai->snapshot_limit);
        }
        cursor = obj_end + 1;
    }
    ai->selfplay_total_interactions = ai->dataset.count;
}

static void parse_memory_from_snapshot(KolibriAI *ai,
                                       const char *array,
                                       const char *array_end) {
    if (!ai || !array || !array_end) {
        return;
    }

    memory_clear(&ai->memory);
    const char *cursor = array;
    while (cursor && cursor < array_end) {
        const char *obj_start = strchr(cursor, '{');
        if (!obj_start || obj_start >= array_end) {
            break;
        }
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end || obj_end > array_end) {
            break;
        }

        KolibriMemoryFact fact = {0};
        parse_string_field_limited(obj_start, obj_end, "key", fact.key, sizeof(fact.key));
        parse_string_field_limited(obj_start, obj_end, "value", fact.value, sizeof(fact.value));
        double salience = 0.0;
        if (parse_number_field_limited(obj_start, obj_end, "salience", &salience) == 0) {
            fact.salience = salience;
        }
        time_t ts = 0;
        if (parse_time_field_limited(obj_start, obj_end, "last_updated", &ts) == 0) {
            fact.last_updated = ts;
        }
        memory_record(&ai->memory, &fact, ai->snapshot_limit);
        cursor = obj_end + 1;
    }
}

int kolibri_ai_import_snapshot(KolibriAI *ai, const char *json) {
    if (!ai || !json) {

        return -1;
    }

    int rc = 0;
    pthread_mutex_lock(&ai->mutex);

    struct json_object *value = NULL;
    if (json_object_object_get_ex(root, "iterations", &value)) {
        ai->iterations = (uint64_t)json_object_get_int64(value);
    }
    if (json_object_object_get_ex(root, "average_reward", &value)) {
        ai->average_reward = json_object_get_double(value);
    }
    if (json_object_object_get_ex(root, "planning_score", &value)) {
        ai->planning_score = json_object_get_double(value);
    }
    if (json_object_object_get_ex(root, "recent_poe", &value)) {
        ai->recent_poe = json_object_get_double(value);
    }
    if (json_object_object_get_ex(root, "recent_mdl", &value)) {
        ai->recent_mdl = json_object_get_double(value);
    }
    if (parse_uint64_field(json, "selfplay_total_interactions", &tmp_u64) == 0) {
        ai->selfplay_total_interactions = tmp_u64;
    }


    if (ai->library) {
        for (size_t i = 0; i < ai->library->count; ++i) {
            formula_clear(&ai->library->formulas[i]);
        }
        ai->library->count = 0;
    }
    dataset_clear(&ai->dataset);
    memory_clear(&ai->memory);

    if (json_object_object_get_ex(root, "formulas", &value) && json_object_is_type(value, json_type_array) && ai->library) {
        size_t formula_count = (size_t)json_object_array_length(value);
        for (size_t i = 0; i < formula_count; ++i) {
            struct json_object *entry = json_object_array_get_idx(value, (int)i);
            if (!entry || !json_object_is_type(entry, json_type_object)) {
                continue;
            }
            struct json_object *field = NULL;
            if (!json_object_object_get_ex(entry, "id", &field)) {
                continue;
            }
            const char *id = json_object_get_string(field);
            if (!id) {
                continue;
            }
            Formula formula = {0};
            formula.representation = FORMULA_REPRESENTATION_TEXT;
            copy_string_truncated(formula.id, sizeof(formula.id), id);
            if (json_object_object_get_ex(entry, "effectiveness", &field)) {
                formula.effectiveness = json_object_get_double(field);
            }
            if (formula_collection_add(ai->library, &formula) != 0) {
                rc = -1;
                break;
            }

    const char *formulas = strstr(json, "\"formulas\":[");
    if (formulas) {
        formulas += strlen("\"formulas\":[");
        const char *end = strchr(formulas, ']');
        if (end) {
            parse_formulas_from_snapshot(ai, formulas, end);
        }
    }

    const char *dataset = strstr(json, "\"dataset\":[");
    if (dataset) {
        dataset += strlen("\"dataset\":[");
        const char *end = strchr(dataset, ']');
        if (end) {
            parse_dataset_from_snapshot(ai, dataset, end);
        }
    }

    const char *memory = strstr(json, "\"memory\":[");
    if (memory) {
        memory += strlen("\"memory\":[");
        const char *end = strchr(memory, ']');
        if (end) {
            parse_memory_from_snapshot(ai, memory, end);

        }
    }

    if (rc == 0 && json_object_object_get_ex(root, "dataset", &value) && json_object_is_type(value, json_type_array)) {
        size_t count = (size_t)json_object_array_length(value);
        size_t limit = ai->snapshot_limit ? ai->snapshot_limit : count;
        size_t start = count > limit ? count - limit : 0;
        for (size_t i = start; i < count; ++i) {
            struct json_object *entry = json_object_array_get_idx(value, (int)i);
            if (!entry || !json_object_is_type(entry, json_type_object)) {
                continue;
            }
            KolibriAIDatasetEntry item = {0};
            struct json_object *field = NULL;
            if (json_object_object_get_ex(entry, "prompt", &field)) {
                copy_string_truncated(item.prompt, sizeof(item.prompt), json_object_get_string(field));
            }
            if (json_object_object_get_ex(entry, "response", &field)) {
                copy_string_truncated(item.response, sizeof(item.response), json_object_get_string(field));
            }
            if (json_object_object_get_ex(entry, "reward", &field)) {
                item.reward = json_object_get_double(field);
            }
            if (json_object_object_get_ex(entry, "poe", &field)) {
                item.poe = json_object_get_double(field);
            }
            if (json_object_object_get_ex(entry, "mdl", &field)) {
                item.mdl = json_object_get_double(field);
            }
            if (json_object_object_get_ex(entry, "timestamp", &field)) {
                item.timestamp = (time_t)json_object_get_int64(field);
            }
            if (dataset_append(&ai->dataset, &item) != 0) {
                dataset_clear(&ai->dataset);
                rc = -1;
                break;
            }
        }
    }

    if (rc == 0 && json_object_object_get_ex(root, "memory", &value) && json_object_is_type(value, json_type_array)) {
        size_t count = (size_t)json_object_array_length(value);
        size_t limit = ai->snapshot_limit ? ai->snapshot_limit : count;
        size_t start = count > limit ? count - limit : 0;
        for (size_t i = start; i < count; ++i) {
            struct json_object *entry = json_object_array_get_idx(value, (int)i);
            if (!entry || !json_object_is_type(entry, json_type_object)) {
                continue;
            }
            KolibriMemoryFact fact = {0};
            struct json_object *field = NULL;
            if (json_object_object_get_ex(entry, "key", &field)) {
                copy_string_truncated(fact.key, sizeof(fact.key), json_object_get_string(field));
            }
            if (json_object_object_get_ex(entry, "value", &field)) {
                copy_string_truncated(fact.value, sizeof(fact.value), json_object_get_string(field));
            }
            if (json_object_object_get_ex(entry, "salience", &field)) {
                fact.salience = json_object_get_double(field);
            }
            if (json_object_object_get_ex(entry, "last_updated", &field)) {
                fact.last_updated = (time_t)json_object_get_int64(field);
            }
            memory_record(&ai->memory, &fact, ai->snapshot_limit);
        }
    }

    if (rc == 0) {
        update_average_reward(ai);
    }

    pthread_mutex_unlock(&ai->mutex);
    json_object_put(root);
    return rc;
}

int kolibri_ai_sync_with_neighbor(KolibriAI *ai, const char *base_url) {
    (void)ai;
    (void)base_url;
    return 0;
}
