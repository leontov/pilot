#define _POSIX_C_SOURCE 200809L

#include "kolibri_ai.h"

#include "formula.h"
#include "util/config.h"
#include "util/log.h"

#include <errno.h>
#include <json-c/json.h>
#include <limits.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct {
    FormulaMemoryFact *facts;
    size_t count;
    size_t capacity;
} KolibriMemoryModule;

struct KolibriAI {
    pthread_t worker;
    int running;
    pthread_mutex_t mutex;

    FormulaCollection *library;
    KolibriMemoryModule memory;
    FormulaDataset dataset;

    double average_reward;
    double exploration_rate;
    double exploitation_rate;
    uint64_t iterations;
    size_t snapshot_limit;
    char snapshot_path[PATH_MAX];
};

typedef struct {
    const char *id;
    const char *content;
    double effectiveness;
} default_formula_t;

static const default_formula_t k_default_formulas[] = {
    {"kolibri.arith.decimal", "f(x, y) = x + y", 0.62},
    {"kolibri.memory.recall", "remember(city) -> answer(city)", 0.58},
    {"kolibri.pattern.sequence", "g(n) = 2*n + 1", 0.64},
};

static void kolibri_ai_seed_library(KolibriAI *ai) {
    if (!ai || !ai->library) {
        return;
    }

    time_t now = time(NULL);
    for (size_t i = 0; i < sizeof(k_default_formulas) / sizeof(k_default_formulas[0]); ++i) {
        Formula formula;
        memset(&formula, 0, sizeof(formula));
        formula.representation = FORMULA_REPRESENTATION_TEXT;
        strncpy(formula.id, k_default_formulas[i].id, sizeof(formula.id) - 1);
        strncpy(formula.content, k_default_formulas[i].content, sizeof(formula.content) - 1);
        formula.effectiveness = k_default_formulas[i].effectiveness;
        formula.created_at = now - (time_t)((sizeof(k_default_formulas) - i) * 90);
        formula.tests_passed = 1;
        formula.confirmations = 1;
        formula_collection_add(ai->library, &formula);
    }

    ai->average_reward = 0.0;
    ai->exploitation_rate = 0.65;
    ai->exploration_rate = 0.35;

    if (ai->library->count > 0) {
        double total = 0.0;
        for (size_t i = 0; i < ai->library->count; ++i) {
            total += ai->library->formulas[i].effectiveness;
        }
        ai->average_reward = total / (double)ai->library->count;
    }
}

static void kolibri_ai_synthesise_formula(KolibriAI *ai) {
    if (!ai || !ai->library) {
        return;
    }

    const size_t max_synthesized = 32;
    if (ai->library->count >= max_synthesized) {
        return;
    }

    double phase = sin((double)ai->iterations / 30.0);
    double effectiveness = 0.55 + 0.15 * phase;

    Formula formula;
    memset(&formula, 0, sizeof(formula));
    formula.representation = FORMULA_REPRESENTATION_TEXT;
    snprintf(formula.id, sizeof(formula.id), "kolibri.synthetic.%zu", ai->library->count + 1);
    snprintf(formula.content, sizeof(formula.content),
             "h_%llu(x) = %.0fx + %.0f", (unsigned long long)ai->iterations,
             round(phase * 5.0) + 2.0, round(phase * 3.0) + 1.0);
    formula.effectiveness = effectiveness;
    formula.created_at = time(NULL);
    formula.tests_passed = 1;

    formula_collection_add(ai->library, &formula);

    if (ai->library->count > 0) {
        double total = 0.0;
        for (size_t i = 0; i < ai->library->count; ++i) {
            total += ai->library->formulas[i].effectiveness;
        }
        ai->average_reward = total / (double)ai->library->count;
    }
}

static void kolibri_memory_module_clear(KolibriMemoryModule *module) {
    if (!module) {
        return;
    }
    free(module->facts);
    module->facts = NULL;
    module->count = 0;
    module->capacity = 0;
}

static void kolibri_ai_dataset_clear(FormulaDataset *dataset) {
    if (!dataset) {
        return;
    }
    free(dataset->entries);
    dataset->entries = NULL;
    dataset->count = 0;
}

static size_t kolibri_ai_clamp_count(size_t total, size_t limit) {
    if (limit == 0 || total <= limit) {
        return total;
    }
    return limit;
}

static size_t kolibri_ai_start_index(size_t total, size_t count) {
    if (total > count) {
        return total - count;
    }
    return 0;
}

static void kolibri_ai_load_dataset_array(KolibriAI *ai, struct json_object *array) {
    if (!ai || !array || !json_object_is_type(array, json_type_array)) {
        return;
    }

    size_t available = (size_t)json_object_array_length(array);
    size_t desired = kolibri_ai_clamp_count(available, ai->snapshot_limit);

    kolibri_ai_dataset_clear(&ai->dataset);
    if (desired == 0) {
        return;
    }

    FormulaDatasetEntry *entries = calloc(desired, sizeof(FormulaDatasetEntry));
    if (!entries) {
        log_warn("kolibri_ai: unable to allocate dataset snapshot (%zu)", desired);
        return;
    }

    size_t start = kolibri_ai_start_index(available, desired);
    size_t idx = 0;
    for (size_t i = start; i < available && idx < desired; ++i) {
        struct json_object *entry = json_object_array_get_idx(array, (int)i);
        if (!entry || !json_object_is_type(entry, json_type_object)) {
            continue;
        }

        FormulaDatasetEntry *target = &entries[idx];
        struct json_object *value = NULL;

        if (json_object_object_get_ex(entry, "task", &value) &&
            json_object_is_type(value, json_type_string)) {
            const char *task = json_object_get_string(value);
            if (task) {
                strncpy(target->task, task, sizeof(target->task) - 1);
            }
        } else if (json_object_object_get_ex(entry, "prompt", &value) &&
                   json_object_is_type(value, json_type_string)) {
            const char *task = json_object_get_string(value);
            if (task) {
                strncpy(target->task, task, sizeof(target->task) - 1);
            }
        }

        if (json_object_object_get_ex(entry, "response", &value) &&
            json_object_is_type(value, json_type_string)) {
            const char *response = json_object_get_string(value);
            if (response) {
                strncpy(target->response, response, sizeof(target->response) - 1);
            }
        } else if (json_object_object_get_ex(entry, "answer", &value) &&
                   json_object_is_type(value, json_type_string)) {
            const char *response = json_object_get_string(value);
            if (response) {
                strncpy(target->response, response, sizeof(target->response) - 1);
            }
        }

        if (json_object_object_get_ex(entry, "effectiveness", &value) &&
            (json_object_is_type(value, json_type_double) || json_object_is_type(value, json_type_int))) {
            target->effectiveness = json_object_get_double(value);
        }

        if (json_object_object_get_ex(entry, "rating", &value) && json_object_is_type(value, json_type_int)) {
            target->rating = json_object_get_int(value);
        }

        if (json_object_object_get_ex(entry, "timestamp", &value) &&
            (json_object_is_type(value, json_type_int) || json_object_is_type(value, json_type_double))) {
            target->timestamp = (time_t)json_object_get_int64(value);
        } else if (json_object_object_get_ex(entry, "time", &value) &&
                   (json_object_is_type(value, json_type_int) || json_object_is_type(value, json_type_double))) {
            target->timestamp = (time_t)json_object_get_int64(value);
        }

        idx++;
    }

    if (idx == 0) {
        free(entries);
        return;
    }

    ai->dataset.entries = entries;
    ai->dataset.count = idx;
}

static void kolibri_ai_load_memory_array(KolibriAI *ai, struct json_object *array) {
    if (!ai || !array || !json_object_is_type(array, json_type_array)) {
        return;
    }

    size_t available = (size_t)json_object_array_length(array);
    size_t desired = kolibri_ai_clamp_count(available, ai->snapshot_limit);

    kolibri_memory_module_clear(&ai->memory);
    if (desired == 0) {
        return;
    }

    FormulaMemoryFact *facts = calloc(desired, sizeof(FormulaMemoryFact));
    if (!facts) {
        log_warn("kolibri_ai: unable to allocate memory snapshot (%zu)", desired);
        return;
    }

    size_t start = kolibri_ai_start_index(available, desired);
    size_t idx = 0;
    for (size_t i = start; i < available && idx < desired; ++i) {
        struct json_object *entry = json_object_array_get_idx(array, (int)i);
        if (!entry || !json_object_is_type(entry, json_type_object)) {
            continue;
        }

        FormulaMemoryFact *fact = &facts[idx];
        struct json_object *value = NULL;

        if (json_object_object_get_ex(entry, "fact_id", &value) &&
            json_object_is_type(value, json_type_string)) {
            const char *id = json_object_get_string(value);
            if (id) {
                strncpy(fact->fact_id, id, sizeof(fact->fact_id) - 1);
            }
        } else if (json_object_object_get_ex(entry, "id", &value) &&
                   json_object_is_type(value, json_type_string)) {
            const char *id = json_object_get_string(value);
            if (id) {
                strncpy(fact->fact_id, id, sizeof(fact->fact_id) - 1);
            }
        }

        if (json_object_object_get_ex(entry, "description", &value) &&
            json_object_is_type(value, json_type_string)) {
            const char *desc = json_object_get_string(value);
            if (desc) {
                strncpy(fact->description, desc, sizeof(fact->description) - 1);
            }
        } else if (json_object_object_get_ex(entry, "content", &value) &&
                   json_object_is_type(value, json_type_string)) {
            const char *desc = json_object_get_string(value);
            if (desc) {
                strncpy(fact->description, desc, sizeof(fact->description) - 1);
            }
        }

        if (json_object_object_get_ex(entry, "importance", &value) &&
            (json_object_is_type(value, json_type_double) || json_object_is_type(value, json_type_int))) {
            fact->importance = json_object_get_double(value);
        }

        if (json_object_object_get_ex(entry, "reward", &value) &&
            (json_object_is_type(value, json_type_double) || json_object_is_type(value, json_type_int))) {
            fact->reward = json_object_get_double(value);
        }

        if (json_object_object_get_ex(entry, "timestamp", &value) &&
            (json_object_is_type(value, json_type_int) || json_object_is_type(value, json_type_double))) {
            fact->timestamp = (time_t)json_object_get_int64(value);
        } else if (json_object_object_get_ex(entry, "time", &value) &&
                   (json_object_is_type(value, json_type_int) || json_object_is_type(value, json_type_double))) {
            fact->timestamp = (time_t)json_object_get_int64(value);
        } else if (json_object_object_get_ex(entry, "created_at", &value) &&
                   (json_object_is_type(value, json_type_int) || json_object_is_type(value, json_type_double))) {
            fact->timestamp = (time_t)json_object_get_int64(value);
        }

        idx++;
    }

    if (idx == 0) {
        free(facts);
        return;
    }

    ai->memory.facts = facts;
    ai->memory.count = idx;
    ai->memory.capacity = desired;
}

static int ensure_parent_directories(const char *path) {
    if (!path || path[0] == '\0') {
        return -1;
    }

    char buffer[PATH_MAX];
    size_t len = strnlen(path, sizeof(buffer));
    if (len == 0 || len >= sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buffer, path, len + 1);

    for (size_t i = 1; i < len; ++i) {
        if (buffer[i] == '/') {
            buffer[i] = '\0';
            if (buffer[0] != '\0') {
                if (mkdir(buffer, 0775) != 0 && errno != EEXIST) {
                    return -1;
                }
            }
            buffer[i] = '/';
        }
    }

    return 0;
}

static void kolibri_ai_load_snapshot(KolibriAI *ai) {
    if (!ai || ai->snapshot_path[0] == '\0') {
        return;
    }

    errno = 0;
    struct json_object *root = json_object_from_file(ai->snapshot_path);
    if (!root) {
        if (errno != ENOENT && errno != 0) {
            log_warn("kolibri_ai: unable to load snapshot %s: %s",
                     ai->snapshot_path,
                     strerror(errno));
        }
        return;
    }

    if (json_object_is_type(root, json_type_array)) {
        kolibri_ai_load_dataset_array(ai, root);
        json_object_put(root);
        return;
    }

    if (!json_object_is_type(root, json_type_object)) {
        json_object_put(root);
        return;
    }

    struct json_object *value = NULL;

    if (json_object_object_get_ex(root, "dataset", &value)) {
        if (json_object_is_type(value, json_type_array)) {
            kolibri_ai_load_dataset_array(ai, value);
        } else if (json_object_is_type(value, json_type_object)) {
            struct json_object *entries = NULL;
            if (json_object_object_get_ex(value, "entries", &entries) &&
                json_object_is_type(entries, json_type_array)) {
                kolibri_ai_load_dataset_array(ai, entries);
            }
        }
    } else if (json_object_object_get_ex(root, "entries", &value) &&
               json_object_is_type(value, json_type_array)) {
        kolibri_ai_load_dataset_array(ai, value);
    }

    struct json_object *memory_obj = NULL;
    if (json_object_object_get_ex(root, "memory", &memory_obj)) {
        if (json_object_is_type(memory_obj, json_type_array)) {
            kolibri_ai_load_memory_array(ai, memory_obj);
        } else if (json_object_is_type(memory_obj, json_type_object)) {
            struct json_object *facts = NULL;
            if (json_object_object_get_ex(memory_obj, "facts", &facts) &&
                json_object_is_type(facts, json_type_array)) {
                kolibri_ai_load_memory_array(ai, facts);
            }
        }
    } else if (json_object_object_get_ex(root, "facts", &memory_obj) &&
               json_object_is_type(memory_obj, json_type_array)) {
        kolibri_ai_load_memory_array(ai, memory_obj);
    }

    json_object_put(root);
}

static int kolibri_ai_persist(KolibriAI *ai) {
    if (!ai || ai->snapshot_path[0] == '\0') {
        return 0;
    }

    struct json_object *root = json_object_new_object();
    struct json_object *dataset_array = json_object_new_array();
    struct json_object *memory_obj = json_object_new_object();
    struct json_object *facts_array = json_object_new_array();

    if (!root || !dataset_array || !memory_obj || !facts_array) {
        if (facts_array) {
            json_object_put(facts_array);
        }
        if (memory_obj) {
            json_object_put(memory_obj);
        }
        if (dataset_array) {
            json_object_put(dataset_array);
        }
        if (root) {
            json_object_put(root);
        }
        return -1;
    }

    pthread_mutex_lock(&ai->mutex);

    size_t dataset_total = ai->dataset.count;
    size_t dataset_keep = kolibri_ai_clamp_count(dataset_total, ai->snapshot_limit);
    size_t dataset_start = kolibri_ai_start_index(dataset_total, dataset_keep);
    for (size_t i = dataset_start; i < dataset_total; ++i) {
        const FormulaDatasetEntry *entry = &ai->dataset.entries[i];
        struct json_object *entry_obj = json_object_new_object();
        if (!entry_obj) {
            continue;
        }
        json_object_object_add(entry_obj, "task", json_object_new_string(entry->task[0] ? entry->task : ""));
        json_object_object_add(entry_obj, "response", json_object_new_string(entry->response[0] ? entry->response : ""));
        json_object_object_add(entry_obj, "effectiveness", json_object_new_double(entry->effectiveness));
        json_object_object_add(entry_obj, "rating", json_object_new_int(entry->rating));
        json_object_object_add(entry_obj, "timestamp", json_object_new_int64((int64_t)entry->timestamp));
        json_object_array_add(dataset_array, entry_obj);
    }

    size_t memory_total = ai->memory.count;
    size_t memory_keep = kolibri_ai_clamp_count(memory_total, ai->snapshot_limit);
    size_t memory_start = kolibri_ai_start_index(memory_total, memory_keep);
    for (size_t i = memory_start; i < memory_total; ++i) {
        const FormulaMemoryFact *fact = &ai->memory.facts[i];
        struct json_object *fact_obj = json_object_new_object();
        if (!fact_obj) {
            continue;
        }
        json_object_object_add(fact_obj, "fact_id", json_object_new_string(fact->fact_id[0] ? fact->fact_id : ""));
        json_object_object_add(fact_obj, "description", json_object_new_string(fact->description[0] ? fact->description : ""));
        json_object_object_add(fact_obj, "importance", json_object_new_double(fact->importance));
        json_object_object_add(fact_obj, "reward", json_object_new_double(fact->reward));
        json_object_object_add(fact_obj, "timestamp", json_object_new_int64((int64_t)fact->timestamp));
        json_object_array_add(facts_array, fact_obj);
    }

    pthread_mutex_unlock(&ai->mutex);

    json_object_object_add(root, "version", json_object_new_int(1));
    json_object_object_add(root, "dataset", dataset_array);
    json_object_object_add(memory_obj, "facts", facts_array);
    json_object_object_add(root, "memory", memory_obj);

    if (ensure_parent_directories(ai->snapshot_path) != 0) {
        log_warn("kolibri_ai: unable to prepare directories for %s: %s",
                 ai->snapshot_path,
                 strerror(errno));
        json_object_put(root);
        return -1;
    }

    char temp_path[PATH_MAX];
    int written = snprintf(temp_path, sizeof(temp_path), "%s.tmp", ai->snapshot_path);
    if (written < 0 || (size_t)written >= sizeof(temp_path)) {
        json_object_put(root);
        errno = ENAMETOOLONG;
        return -1;
    }

    const char *json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY);
    size_t json_len = strlen(json_str);

    FILE *fp = fopen(temp_path, "w");
    if (!fp) {
        log_warn("kolibri_ai: unable to open %s for writing: %s", temp_path, strerror(errno));
        json_object_put(root);
        return -1;
    }

    size_t bytes = fwrite(json_str, 1, json_len, fp);
    int flush_rc = fflush(fp);
    if (bytes != json_len || flush_rc != 0) {
        log_warn("kolibri_ai: failed to write snapshot %s", temp_path);
        fclose(fp);
        remove(temp_path);
        json_object_put(root);
        return -1;
    }

    if (fclose(fp) != 0) {
        log_warn("kolibri_ai: failed to close snapshot %s", temp_path);
        remove(temp_path);
        json_object_put(root);
        return -1;
    }

    if (rename(temp_path, ai->snapshot_path) != 0) {
        log_warn("kolibri_ai: failed to replace snapshot %s: %s",
                 ai->snapshot_path,
                 strerror(errno));
        remove(temp_path);
        json_object_put(root);
        return -1;
    }

    json_object_put(root);
    return 0;
}

KolibriAI *kolibri_ai_create(void) {
    KolibriAI *ai = calloc(1, sizeof(KolibriAI));
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

    ai->iterations = 0;
    ai->average_reward = 0.0;
    ai->exploration_rate = 0.4;
    ai->exploitation_rate = 0.6;

    kolibri_config_t cfg;
    if (config_load("cfg/kolibri.jsonc", &cfg) != 0) {
        log_warn("kolibri_ai: using default persistence configuration");
    }
    if (cfg.ai.snapshot_path[0] != '\0') {
        strncpy(ai->snapshot_path, cfg.ai.snapshot_path, sizeof(ai->snapshot_path) - 1);
        ai->snapshot_path[sizeof(ai->snapshot_path) - 1] = '\0';
    }
    ai->snapshot_limit = cfg.ai.snapshot_limit;

    kolibri_ai_load_snapshot(ai);
    kolibri_ai_seed_library(ai);
    return ai;
}

void kolibri_ai_destroy(KolibriAI *ai) {
    if (!ai) {
        return;
    }

    kolibri_ai_stop(ai);
    pthread_mutex_destroy(&ai->mutex);
    kolibri_ai_dataset_clear(&ai->dataset);
    kolibri_memory_module_clear(&ai->memory);
    if (ai->library) {
        formula_collection_destroy(ai->library);
    }
    free(ai);
}

static void *kolibri_ai_worker(void *arg) {
    KolibriAI *ai = (KolibriAI *)arg;
    while (1) {
        pthread_mutex_lock(&ai->mutex);
        int should_continue = ai->running;
        pthread_mutex_unlock(&ai->mutex);

        if (!should_continue) {
            break;
        }

        kolibri_ai_process_iteration(ai);
        struct timespec req = {0, 75000 * 1000};
        nanosleep(&req, NULL);
    }
    return NULL;
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

    pthread_create(&ai->worker, NULL, kolibri_ai_worker, ai);
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

    double phase = sin((double)ai->iterations / 24.0);
    double exploitation_delta = 0.02 * phase;
    double exploration_delta = 0.015 * cos((double)ai->iterations / 18.0);

    ai->exploitation_rate = fmin(0.9, fmax(0.5, ai->exploitation_rate + exploitation_delta));
    ai->exploration_rate = fmin(0.5, fmax(0.1, ai->exploration_rate + exploration_delta));

    if (ai->iterations % 60 == 0) {
        kolibri_ai_synthesise_formula(ai);
    }

    pthread_mutex_unlock(&ai->mutex);
}

int kolibri_ai_add_formula(KolibriAI *ai, const Formula *formula) {
    if (!ai || !formula) {
        return -1;
    }

    pthread_mutex_lock(&ai->mutex);
    int rc = formula_collection_add(ai->library, formula);
    if (rc == 0 && ai->library->count > 0) {
        double total = 0.0;
        for (size_t i = 0; i < ai->library->count; ++i) {
            total += ai->library->formulas[i].effectiveness;
        }
        ai->average_reward = total / (double)ai->library->count;
    }
    pthread_mutex_unlock(&ai->mutex);
    return rc;
}

Formula *kolibri_ai_get_best_formula(KolibriAI *ai) {
    if (!ai || !ai->library) {
        return NULL;
    }

    pthread_mutex_lock(&ai->mutex);
    const Formula *top[1] = {0};
    Formula *copy = NULL;
    if (formula_collection_get_top(ai->library, top, 1) == 1) {
        copy = calloc(1, sizeof(Formula));
        if (copy && formula_copy(copy, top[0]) != 0) {
            free(copy);
            copy = NULL;
        }
    }
    pthread_mutex_unlock(&ai->mutex);
    return copy;
}

static char *kolibri_ai_alloc_json(size_t initial) {
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
    uint64_t iterations = ai->iterations;
    size_t formula_count = ai->library ? ai->library->count : 0;
    double avg_reward = ai->average_reward;
    double exploitation = ai->exploitation_rate;
    double exploration = ai->exploration_rate;
    int running = ai->running;
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    char temp[256];
    int written = snprintf(temp, sizeof(temp),
                           "{\"iterations\":%llu,\"formula_count\":%zu,\"average_reward\":%.3f,"
                           "\"exploitation_rate\":%.3f,\"exploration_rate\":%.3f,\"running\":%d}",
                           (unsigned long long)iterations,
                           formula_count,
                           avg_reward,
                           exploitation,
                           exploration,
                           running);
    if (written < 0) {
        return NULL;
    }
    char *json = malloc((size_t)written + 1);
    if (!json) {
        return NULL;
    }
    memcpy(json, temp, (size_t)written + 1);
    return json;
}

char *kolibri_ai_serialize_formulas(const KolibriAI *ai, size_t max_results) {
    if (!ai || max_results == 0) {
        return NULL;
    }

    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    const Formula *top[16] = {0};
    if (max_results > sizeof(top) / sizeof(top[0])) {
        max_results = sizeof(top) / sizeof(top[0]);
    }
    size_t count = ai->library ? formula_collection_get_top(ai->library, top, max_results) : 0;
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

    size_t capacity = 256;
    char *json = kolibri_ai_alloc_json(capacity);
    if (!json) {
        return NULL;
    }

    size_t len = 0;
    int needed = snprintf(json, capacity, "{\"formulas\":[");
    if (needed < 0) {
        free(json);
        return NULL;
    }
    len = (size_t)needed;

    for (size_t i = 0; i < count; ++i) {
        const Formula *formula = top[i];
        if (!formula) {
            continue;
        }

        char iso_time[32];
        struct tm tm_buf;
        time_t created = formula->created_at;
        gmtime_r(&created, &tm_buf);
        strftime(iso_time, sizeof(iso_time), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);

        char entry[512];
        needed = snprintf(entry, sizeof(entry),
                          "%s{\"id\":\"%s\",\"content\":\"%s\",\"effectiveness\":%.3f,\"created_at\":\"%s\"}",
                          (i == 0) ? "" : ",",
                          formula->id,
                          formula->content,
                          formula->effectiveness,
                          iso_time);
        if (needed < 0) {
            free(json);
            return NULL;
        }

        if (len + (size_t)needed + 2 > capacity) {
            size_t new_capacity = capacity;
            while (len + (size_t)needed + 2 > new_capacity) {
                new_capacity *= 2;
            }
            char *tmp = realloc(json, new_capacity);
            if (!tmp) {
                free(json);
                return NULL;
            }
            json = tmp;
            capacity = new_capacity;
        }

        memcpy(json + len, entry, (size_t)needed);
        len += (size_t)needed;
        json[len] = '\0';
    }

    if (len + 2 > capacity) {
        char *tmp = realloc(json, len + 2);
        if (!tmp) {
            free(json);
            return NULL;
        }
        json = tmp;
        capacity = len + 2;
    }

    snprintf(json + len, capacity - len, "]}");
    return json;
}
