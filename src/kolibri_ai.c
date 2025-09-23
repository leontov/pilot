#define _POSIX_C_SOURCE 200809L

#include "kolibri_ai.h"

#include "formula.h"
#include "util/config.h"
#include "util/log.h"


#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {


struct KolibriAI {
    pthread_t worker;
    int running;
    pthread_mutex_t mutex;

    FormulaCollection *library;
    FormulaTrainingPipeline *pipeline;


    double average_reward;
    double exploration_rate;
    double exploitation_rate;
    double planning_score;
    double recent_poe;
    double recent_mdl;
    uint64_t iterations;


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
        state->success_ema[i] = 0.7;
        state->reward_ema[i] = 0.55;
        state->sample_count[i] = 0;
    }

    state->global_success_ema = 0.7;
    state->integral_error = 0.0;
    state->last_error = 0.0;
    state->temperature = 0.6;
    state->ema_alpha = 0.18;
    state->current_level = KOLIBRI_DIFFICULTY_FOUNDATION;
}

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


    }
    return value;
}


}

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
        for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
            ai->curriculum.reward_ema[i] = ai->average_reward;
        }
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


}

KolibriAI *kolibri_ai_create(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    KolibriAI *ai = calloc(1, sizeof(KolibriAI));
    if (!ai) {
        return NULL;
    }

    if (pthread_mutex_init(&ai->mutex, NULL) != 0) {
        free(ai);
        return NULL;
    }

    ai->pipeline = formula_training_pipeline_create(12);
    if (!ai->pipeline) {
        pthread_mutex_destroy(&ai->mutex);
        free(ai);
        return NULL;
    }

    ai->library = formula_collection_create(8);
    if (!ai->library) {
        formula_training_pipeline_destroy(ai->pipeline);
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

    ai->iterations = 0;
    ai->average_reward = 0.0;
    ai->exploration_rate = 0.4;
    ai->exploitation_rate = 0.6;
    ai->recent_reward = 0.0;
    kolibri_curriculum_init(&ai->curriculum);



    kolibri_ai_seed_library(ai);
    return ai;
}

void kolibri_ai_destroy(KolibriAI *ai) {
    if (!ai) {
        return;
    }

    kolibri_ai_stop(ai);
    pthread_mutex_destroy(&ai->mutex);
    if (ai->library) {
        formula_collection_destroy(ai->library);
    }
    if (ai->pipeline) {
        formula_training_pipeline_destroy(ai->pipeline);
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
    if (!ai->running) {
        pthread_mutex_unlock(&ai->mutex);
        return;
    }
    ai->running = 0;
    pthread_mutex_unlock(&ai->mutex);

    pthread_join(ai->worker, NULL);
}

void kolibri_ai_set_selfplay_config(KolibriAI *ai, const KolibriAISelfplayConfig *config) {
    if (!ai || !config) {
        return;
    }

    pthread_mutex_lock(&ai->mutex);
    ai->selfplay_config.tasks_per_iteration = config->tasks_per_iteration;
    uint32_t max_difficulty = config->max_difficulty;
    if (max_difficulty == 0) {
        max_difficulty = 1;
    }
    ai->selfplay_config.max_difficulty = max_difficulty;
    if (ai->selfplay_current_difficulty == 0) {
        ai->selfplay_current_difficulty = 1;
    }
    if (ai->selfplay_current_difficulty > ai->selfplay_config.max_difficulty) {
        ai->selfplay_current_difficulty = ai->selfplay_config.max_difficulty;
    }
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_record_interaction(KolibriAI *ai, const KolibriAISelfplayInteraction *interaction) {
    if (!ai || !interaction) {
        return;
    }

    log_info("self-play: %s | expected=%.3f predicted=%.3f error=%.3f reward=%.3f success=%s diff=%u",
             interaction->task.description,
             interaction->task.expected_result,
             interaction->predicted_result,
             interaction->error,
             interaction->reward,
             interaction->success ? "yes" : "no",
             interaction->task.difficulty);

    ai->selfplay_total_interactions++;
    double count = (double)ai->selfplay_total_interactions;
    if (count <= 0.0) {
        count = 1.0;
    }
    ai->selfplay_reward_avg += (interaction->reward - ai->selfplay_reward_avg) / count;

    ai->selfplay_recent_total++;
    if (interaction->success) {
        ai->selfplay_recent_success++;
    }

    size_t window = ai->selfplay_config.tasks_per_iteration;
    if (window == 0) {
        window = 1;
    }
    if (ai->selfplay_recent_total >= window) {
        double ratio = (double)ai->selfplay_recent_success;
        ratio /= (double)ai->selfplay_recent_total;
        if (ratio > 0.75 && ai->selfplay_current_difficulty < ai->selfplay_config.max_difficulty) {
            ai->selfplay_current_difficulty++;
        } else if (ratio < 0.35 && ai->selfplay_current_difficulty > 1) {
            ai->selfplay_current_difficulty--;
        }
        ai->selfplay_recent_total = 0;
        ai->selfplay_recent_success = 0;
    }
}

void kolibri_ai_apply_config(KolibriAI *ai, const struct kolibri_config_t *cfg) {
    if (!ai || !cfg) {
        return;
    }

    KolibriAISelfplayConfig sp_config = {
        .tasks_per_iteration = cfg->selfplay.tasks_per_iteration,
        .max_difficulty = cfg->selfplay.max_difficulty,
    };
    kolibri_ai_set_selfplay_config(ai, &sp_config);

    pthread_mutex_lock(&ai->mutex);
    if (cfg->seed != 0) {
        ai->rng_state = cfg->seed;
    }
    pthread_mutex_unlock(&ai->mutex);
}

void kolibri_ai_process_iteration(KolibriAI *ai) {
    if (!ai) {
        return;
    }

    double expected_reward = 0.0;
    KolibriDifficultyLevel level = kolibri_ai_plan_actions(ai, &expected_reward);

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

    static const double difficulty_scaling[KOLIBRI_DIFFICULTY_COUNT] = {0.82, 1.0, 1.14, 1.28};

    double base_reward = expected_reward > 0.0 ? expected_reward : baseline_reward;
    if (base_reward <= 0.0) {
        base_reward = 0.5 + 0.05 * sin((double)current_iteration / 12.0);
    }

    double reward = base_reward * difficulty_scaling[level];
    reward += 0.02 * sin((double)current_iteration / 8.0);
    if (reward < 0.0) {
        reward = 0.0;
    }

    double success_threshold = baseline_reward > 0.0 ? baseline_reward : 0.45;
    success_threshold *= 0.88 + 0.04 * (double)level;
    int success = reward >= success_threshold;

    kolibri_ai_apply_reinforcement(ai, level, reward, success);
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
    }
    pthread_mutex_unlock(&ai->mutex);
    return rc;
}

int kolibri_ai_apply_reinforcement(KolibriAI *ai,
                                   const Formula *formula,
                                   const FormulaExperience *experience) {
    if (!ai || !experience) {
        return -1;
    }

    double reward = fmax(0.0, fmin(1.0, experience->reward));
    double poe = fmax(0.0, fmin(1.0, experience->poe));
    double mdl = experience->mdl < 0.0 ? 0.0 : experience->mdl;
    double planning_update = poe - 0.35 * mdl;
    if (planning_update < 0.0) {
        planning_update = 0.0;
    }

    pthread_mutex_lock(&ai->mutex);
    ai->recent_poe = poe;
    ai->recent_mdl = mdl;
    ai->average_reward = 0.9 * ai->average_reward + 0.1 * reward;
    if (ai->planning_score <= 0.0) {
        ai->planning_score = planning_update;
    } else {
        ai->planning_score = 0.8 * ai->planning_score + 0.2 * planning_update;
    }

    if (ai->library && formula) {
        Formula *existing = formula_collection_find(ai->library, formula->id);
        if (existing) {
            if (reward > existing->effectiveness) {
                existing->effectiveness = reward;
            }
        } else if (poe >= 0.55 && planning_update >= ai->planning_score * 0.9) {
            Formula copy = {0};
            if (formula_copy(&copy, formula) == 0) {
                copy.effectiveness = reward;
                formula_collection_add(ai->library, &copy);
                formula_clear(&copy);
            }
        }
    }
    pthread_mutex_unlock(&ai->mutex);
    return 0;
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

static int kolibri_format_double_array(const double *values, size_t count, char *buffer, size_t size) {
    if (!values || !buffer || size == 0) {
        return -1;
    }

    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        int written = snprintf(buffer + offset, size - offset, "%s%.3f", (i == 0) ? "" : ",", values[i]);
        if (written < 0 || (size_t)written >= size - offset) {
            return -1;
        }
        offset += (size_t)written;
    }
    return 0;
}

static int kolibri_format_uint64_array(const uint64_t *values, size_t count, char *buffer, size_t size) {
    if (!values || !buffer || size == 0) {
        return -1;
    }

    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
        int written = snprintf(buffer + offset, size - offset, "%s%llu", (i == 0) ? "" : ",",
                               (unsigned long long)values[i]);
        if (written < 0 || (size_t)written >= size - offset) {
            return -1;
        }
        offset += (size_t)written;
    }
    return 0;
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
    double planning = ai->planning_score;
    double recent_poe = ai->recent_poe;
    double recent_mdl = ai->recent_mdl;
    int running = ai->running;

                           (unsigned long long)iterations,
                           formula_count,
                           avg_reward,
                           recent_reward,
                           exploitation,
                           exploration,

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

int kolibri_ai_process_remote_formula(KolibriAI *ai,
                                      const Formula *formula,
                                      const FormulaExperience *experience) {
    if (!ai || !formula || !experience) {
        return -1;
    }

    pthread_mutex_lock(&ai->mutex);
    int rc = 0;
    Formula *existing = ai->library ? formula_collection_find(ai->library, formula->id) : NULL;
    if (existing) {
        rc = formula_copy(existing, formula);
    } else if (ai->library) {
        rc = formula_collection_add(ai->library, formula);
    } else {
        rc = -1;
    }

    if (rc == 0 && ai->library && ai->library->count > 0) {
        double total = 0.0;
        for (size_t i = 0; i < ai->library->count; ++i) {
            total += ai->library->formulas[i].effectiveness;
        }
        ai->average_reward = total / (double)ai->library->count;
        FormulaExperience *slot = kolibri_ai_get_experience_record(ai, formula->id, 1);
        if (slot) {
            *slot = *experience;
        }
        if (ai->pipeline) {
            formula_training_pipeline_record_experience(ai->pipeline, experience);
        }
    }
    pthread_mutex_unlock(&ai->mutex);
    return rc;
}

char *kolibri_ai_export_snapshot(const KolibriAI *ai) {
    if (!ai) {
        return NULL;
    }

    pthread_mutex_lock((pthread_mutex_t *)&ai->mutex);
    struct json_object *root = json_object_new_object();
    struct json_object *dataset = json_object_new_array();
    struct json_object *memory = json_object_new_array();
    struct json_object *formulas = json_object_new_array();

    if (ai->pipeline) {
        for (size_t i = 0; i < ai->pipeline->dataset.count; ++i) {
            const FormulaDatasetEntry *entry = &ai->pipeline->dataset.entries[i];
            struct json_object *obj = json_object_new_object();
            json_object_object_add(obj, "task", json_object_new_string(entry->task));
            json_object_object_add(obj, "response", json_object_new_string(entry->response));
            json_object_object_add(obj, "effectiveness", json_object_new_double(entry->effectiveness));
            json_object_object_add(obj, "rating", json_object_new_int(entry->rating));
            json_object_object_add(obj, "timestamp", json_object_new_int64(entry->timestamp));
            json_object_array_add(dataset, obj);
        }

        for (size_t i = 0; i < ai->pipeline->memory_snapshot.count; ++i) {
            const FormulaMemoryFact *fact = &ai->pipeline->memory_snapshot.facts[i];
            struct json_object *obj = json_object_new_object();
            json_object_object_add(obj, "fact_id", json_object_new_string(fact->fact_id));
            json_object_object_add(obj, "description", json_object_new_string(fact->description));
            json_object_object_add(obj, "importance", json_object_new_double(fact->importance));
            json_object_object_add(obj, "reward", json_object_new_double(fact->reward));
            json_object_object_add(obj, "timestamp", json_object_new_int64(fact->timestamp));
            json_object_array_add(memory, obj);
        }
    }

    if (ai->library) {
        for (size_t i = 0; i < ai->library->count; ++i) {
            const Formula *formula = &ai->library->formulas[i];
            struct json_object *obj = json_object_new_object();
            json_object_object_add(obj, "id", json_object_new_string(formula->id));
            json_object_object_add(obj, "representation", json_object_new_int(formula->representation));
            json_object_object_add(obj, "effectiveness", json_object_new_double(formula->effectiveness));
            json_object_object_add(obj, "created_at", json_object_new_int64(formula->created_at));
            json_object_object_add(obj, "tests_passed", json_object_new_int(formula->tests_passed));
            json_object_object_add(obj, "confirmations", json_object_new_int(formula->confirmations));
            if (formula->representation == FORMULA_REPRESENTATION_TEXT) {
                json_object_object_add(obj, "content", json_object_new_string(formula->content));
            }

            FormulaExperience *exp = kolibri_ai_get_experience_record((KolibriAI *)ai, formula->id, 0);
            if (exp) {
                struct json_object *exp_obj = json_object_new_object();
                json_object_object_add(exp_obj, "reward", json_object_new_double(exp->reward));
                json_object_object_add(exp_obj, "imitation_score", json_object_new_double(exp->imitation_score));
                json_object_object_add(exp_obj, "accuracy", json_object_new_double(exp->accuracy));
                json_object_object_add(exp_obj, "loss", json_object_new_double(exp->loss));
                json_object_object_add(exp_obj, "source", json_object_new_string(exp->source));
                json_object_object_add(exp_obj, "task_id", json_object_new_string(exp->task_id));
                json_object_object_add(obj, "experience", exp_obj);
            }
            json_object_array_add(formulas, obj);
        }
    }

    json_object_object_add(root, "dataset", dataset);
    json_object_object_add(root, "memory", memory);
    json_object_object_add(root, "formulas", formulas);

    const char *json_str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    char *result = NULL;
    if (json_str) {
        result = strdup(json_str);
    }
    json_object_put(root);
    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
    return result;
}

int kolibri_ai_import_snapshot(KolibriAI *ai, const char *json_payload) {
    if (!ai || !json_payload) {
        return -1;
    }

    struct json_tokener *tok = json_tokener_new();
    if (!tok) {
        return -1;
    }
    struct json_object *root = json_tokener_parse_ex(tok, json_payload, (int)strlen(json_payload));
    enum json_tokener_error err = json_tokener_get_error(tok);
    json_tokener_free(tok);
    if (err != json_tokener_success || !root) {
        if (root) {
            json_object_put(root);
        }
        return -1;
    }
    if (!json_object_is_type(root, json_type_object)) {
        json_object_put(root);
        return -1;
    }

    FormulaDatasetEntry *dataset_entries = NULL;
    size_t dataset_count = 0;
    FormulaMemoryFact *memory_facts = NULL;
    size_t memory_count = 0;

    struct json_object *dataset_obj = NULL;
    if (json_object_object_get_ex(root, "dataset", &dataset_obj) &&
        json_object_is_type(dataset_obj, json_type_array)) {
        size_t count = json_object_array_length(dataset_obj);
        if (count > 0) {
            dataset_entries = calloc(count, sizeof(FormulaDatasetEntry));
            if (!dataset_entries) {
                json_object_put(root);
                return -1;
            }
            for (size_t i = 0; i < count; ++i) {
                struct json_object *entry = json_object_array_get_idx(dataset_obj, (int)i);
                if (!entry) {
                    continue;
                }
                FormulaDatasetEntry *target = &dataset_entries[dataset_count];
                struct json_object *value = NULL;
                if (json_object_object_get_ex(entry, "task", &value)) {
                    const char *task = json_object_get_string(value);
                    if (task) {
                        strncpy(target->task, task, sizeof(target->task) - 1);
                    }
                }
                if (json_object_object_get_ex(entry, "response", &value)) {
                    const char *response = json_object_get_string(value);
                    if (response) {
                        strncpy(target->response, response, sizeof(target->response) - 1);
                    }
                }
                if (json_object_object_get_ex(entry, "effectiveness", &value)) {
                    target->effectiveness = json_object_get_double(value);
                }
                if (json_object_object_get_ex(entry, "rating", &value)) {
                    target->rating = json_object_get_int(value);
                }
                if (json_object_object_get_ex(entry, "timestamp", &value)) {
                    target->timestamp = (time_t)json_object_get_int64(value);
                }
                dataset_count++;
            }
        }
    }

    struct json_object *memory_obj = NULL;
    if (json_object_object_get_ex(root, "memory", &memory_obj) &&
        json_object_is_type(memory_obj, json_type_array)) {
        size_t count = json_object_array_length(memory_obj);
        if (count > 0) {
            memory_facts = calloc(count, sizeof(FormulaMemoryFact));
            if (!memory_facts) {
                free(dataset_entries);
                json_object_put(root);
                return -1;
            }
            for (size_t i = 0; i < count; ++i) {
                struct json_object *fact_obj = json_object_array_get_idx(memory_obj, (int)i);
                if (!fact_obj) {
                    continue;
                }
                FormulaMemoryFact *target = &memory_facts[memory_count];
                struct json_object *value = NULL;
                if (json_object_object_get_ex(fact_obj, "fact_id", &value)) {
                    const char *fact_id = json_object_get_string(value);
                    if (fact_id) {
                        strncpy(target->fact_id, fact_id, sizeof(target->fact_id) - 1);
                    }
                }
                if (json_object_object_get_ex(fact_obj, "description", &value)) {
                    const char *desc = json_object_get_string(value);
                    if (desc) {
                        strncpy(target->description, desc, sizeof(target->description) - 1);
                    }
                }
                if (json_object_object_get_ex(fact_obj, "importance", &value)) {
                    target->importance = json_object_get_double(value);
                }
                if (json_object_object_get_ex(fact_obj, "reward", &value)) {
                    target->reward = json_object_get_double(value);
                }
                if (json_object_object_get_ex(fact_obj, "timestamp", &value)) {
                    target->timestamp = (time_t)json_object_get_int64(value);
                }
                memory_count++;
            }
        }
    }

    pthread_mutex_lock(&ai->mutex);
    kolibri_ai_merge_dataset(ai, dataset_entries, dataset_count);
    kolibri_ai_merge_memory(ai, memory_facts, memory_count);
    pthread_mutex_unlock(&ai->mutex);

    struct json_object *formulas_obj = NULL;
    if (json_object_object_get_ex(root, "formulas", &formulas_obj) &&
        json_object_is_type(formulas_obj, json_type_array)) {
        size_t count = json_object_array_length(formulas_obj);
        for (size_t i = 0; i < count; ++i) {
            struct json_object *formula_obj = json_object_array_get_idx(formulas_obj, (int)i);
            if (!formula_obj) {
                continue;
            }
            Formula formula = {0};
            FormulaExperience experience = {0};
            struct json_object *value = NULL;
            if (json_object_object_get_ex(formula_obj, "id", &value)) {
                const char *id = json_object_get_string(value);
                if (id) {
                    strncpy(formula.id, id, sizeof(formula.id) - 1);
                }
            }
            if (json_object_object_get_ex(formula_obj, "representation", &value)) {
                formula.representation = (FormulaRepresentation)json_object_get_int(value);
            } else {
                formula.representation = FORMULA_REPRESENTATION_TEXT;
            }
            if (json_object_object_get_ex(formula_obj, "effectiveness", &value)) {
                formula.effectiveness = json_object_get_double(value);
            }
            if (json_object_object_get_ex(formula_obj, "created_at", &value)) {
                formula.created_at = (time_t)json_object_get_int64(value);
            }
            if (json_object_object_get_ex(formula_obj, "tests_passed", &value)) {
                formula.tests_passed = (uint32_t)json_object_get_int(value);
            }
            if (json_object_object_get_ex(formula_obj, "confirmations", &value)) {
                formula.confirmations = (uint32_t)json_object_get_int(value);
            }
            if (formula.representation == FORMULA_REPRESENTATION_TEXT) {
                if (json_object_object_get_ex(formula_obj, "content", &value)) {
                    const char *content = json_object_get_string(value);
                    if (content) {
                        strncpy(formula.content, content, sizeof(formula.content) - 1);
                    }
                }
            }

            struct json_object *exp_obj = NULL;
            if (json_object_object_get_ex(formula_obj, "experience", &exp_obj) &&
                json_object_is_type(exp_obj, json_type_object)) {
                if (json_object_object_get_ex(exp_obj, "reward", &value)) {
                    experience.reward = json_object_get_double(value);
                }
                if (json_object_object_get_ex(exp_obj, "imitation_score", &value)) {
                    experience.imitation_score = json_object_get_double(value);
                }
                if (json_object_object_get_ex(exp_obj, "accuracy", &value)) {
                    experience.accuracy = json_object_get_double(value);
                }
                if (json_object_object_get_ex(exp_obj, "loss", &value)) {
                    experience.loss = json_object_get_double(value);
                }
                if (json_object_object_get_ex(exp_obj, "source", &value)) {
                    const char *source = json_object_get_string(value);
                    if (source) {
                        strncpy(experience.source, source, sizeof(experience.source) - 1);
                    }
                }
                if (json_object_object_get_ex(exp_obj, "task_id", &value)) {
                    const char *task_id = json_object_get_string(value);
                    if (task_id) {
                        strncpy(experience.task_id, task_id, sizeof(experience.task_id) - 1);
                    }
                }
            }

            kolibri_ai_process_remote_formula(ai, &formula, &experience);
        }
    }

    free(dataset_entries);
    free(memory_facts);
    json_object_put(root);
    return 0;
}

int kolibri_ai_sync_with_neighbor(KolibriAI *ai, const char *neighbor_base_url) {
    if (!ai || !neighbor_base_url) {
        return -1;
    }

    char *snapshot = kolibri_ai_export_snapshot(ai);
    if (!snapshot) {
        return -1;
    }

    char url[512];
    int needed = snprintf(url, sizeof(url), "%s/api/v1/ai/snapshot", neighbor_base_url);
    if (needed < 0 || (size_t)needed >= sizeof(url)) {
        free(snapshot);
        return -1;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(snapshot);
        return -1;
    }

    CURLcode overall = CURLE_OK;
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, snapshot);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(snapshot));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    overall = curl_easy_perform(curl);

    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
    curl_slist_free_all(headers);
    headers = NULL;

    curl_buffer_t buffer = {0};
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, kolibri_ai_curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    CURLcode get_res = curl_easy_perform(curl);
    if (get_res == CURLE_OK && buffer.data) {
        kolibri_ai_import_snapshot(ai, buffer.data);
    }

    curl_easy_cleanup(curl);
    free(snapshot);
    free(buffer.data);
    return (overall == CURLE_OK || get_res == CURLE_OK) ? 0 : -1;
}
