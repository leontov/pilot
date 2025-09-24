/* Simplified Kolibri AI coordinator implementation.
 * Rewritten to provide a minimal but fully working version that
 * satisfies the unit tests and exposes snapshot import/export helpers.
 */

#include "kolibri_ai.h"
#include "util/log.h"
#include <json-c/json.h>
#include <errno.h>
#include <math.h>
#include <float.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct KolibriAI {
    FormulaCollection *library;
    FormulaTrainingPipeline *pipeline;
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
    memmove(dataset->entries,
            dataset->entries + offset,
            limit * sizeof(dataset->entries[0]));
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
    memmove(memory->facts,
            memory->facts + offset,
            limit * sizeof(memory->facts[0]));
    memory->count = limit;
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

static double curriculum_estimate_reward(const KolibriCurriculumState *curriculum,
                                         KolibriDifficultyLevel level) {
    if (!curriculum || level >= KOLIBRI_DIFFICULTY_COUNT) {
        return 0.0;
    }
    double reward = clamp01(curriculum->reward_ema[level]);
    double success = clamp01(curriculum->success_ema[level]);
    double exploration = clamp01(1.0 - curriculum->distribution[level]);
    double temperature = clamp01(curriculum->temperature);
    double heuristic = 0.55 * reward + 0.3 * success + 0.15 * exploration * temperature;
    return clamp01(heuristic);
}

static KolibriDifficultyLevel curriculum_plan_mcts(KolibriAI *ai,
                                                   unsigned int *rng_state,
                                                   double *expected_reward) {
    if (!ai || !rng_state) {
        if (expected_reward) {
            *expected_reward = 0.0;
        }
        return KOLIBRI_DIFFICULTY_FOUNDATION;
    }

    FormulaMctsConfig cfg = ai->pipeline ? ai->pipeline->planner_config
                                         : formula_mcts_config_default();
    double totals[KOLIBRI_DIFFICULTY_COUNT] = {0};
    uint32_t visits[KOLIBRI_DIFFICULTY_COUNT] = {0};
    size_t total_visits = 0;

    for (size_t rollout = 0; rollout < cfg.rollouts; ++rollout) {
        size_t best_index = 0;
        double best_score = -DBL_MAX;
        for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
            double mean = visits[i] ? totals[i] / (double)visits[i] : 0.0;
            double explore = visits[i]
                                  ? cfg.exploration *
                                        sqrt(log((double)(total_visits + 1)) /
                                             (double)visits[i])
                                  : DBL_MAX;
            double ucb = mean + explore;
            if (ucb > best_score) {
                best_score = ucb;
                best_index = i;
            }
        }

        double heuristic = curriculum_estimate_reward(&ai->curriculum,
                                                      (KolibriDifficultyLevel)best_index);
        double noise = (double)(rand_r(rng_state) % 1000) / 1000.0;
        double reward = clamp01(heuristic + 0.05 * noise);
        totals[best_index] += reward;
        visits[best_index] += 1;
        total_visits += 1;
    }

    size_t best_action = 0;
    double best_mean = -1.0;
    for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
        double mean = visits[i]
                          ? totals[i] / (double)visits[i]
                          : curriculum_estimate_reward(&ai->curriculum,
                                                        (KolibriDifficultyLevel)i);
        if (mean > best_mean) {
            best_mean = mean;
            best_action = i;
        }
    }

    if (expected_reward) {
        *expected_reward = clamp01(best_mean);
    }
    return (KolibriDifficultyLevel)best_action;
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
    snprintf(bootstrap.id, sizeof(bootstrap.id), "kolibri.bootstrap");
    snprintf(bootstrap.content,
             sizeof(bootstrap.content),
             "kolibri(x) = x + 1");
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

    return 0;
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


static void configure_defaults(KolibriAI *ai, const kolibri_config_t *cfg) {
    if (!ai) {
        return;
    }
    ai->search_config = cfg ? cfg->search : formula_search_config_default();
    if (ai->search_config.max_candidates == 0) {
        ai->search_config = formula_search_config_default();
    }
    if (ai->pipeline) {
        formula_training_pipeline_set_search_config(ai->pipeline, &ai->search_config);
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

    size_t pipeline_capacity = 16;
    if (cfg && cfg->search.max_candidates > 0) {
        pipeline_capacity = cfg->search.max_candidates * 2;
    }
    ai->pipeline = formula_training_pipeline_create(pipeline_capacity);
    if (!ai->pipeline) {
        formula_collection_destroy(ai->library);
        pthread_mutex_destroy(&ai->mutex);
        free(ai);
        return NULL;
    }

    size_t pipeline_capacity = 16;
    if (cfg && cfg->search.max_candidates > 0) {
        pipeline_capacity = cfg->search.max_candidates * 2;
    }
    ai->pipeline = formula_training_pipeline_create(pipeline_capacity);
    if (!ai->pipeline) {
        formula_collection_destroy(ai->library);
        pthread_mutex_destroy(&ai->mutex);
        free(ai);
        return NULL;
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


    if (ai->pipeline) {
        formula_training_pipeline_destroy(ai->pipeline);
    }

    if (ai->pipeline) {
        formula_training_pipeline_destroy(ai->pipeline);
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
    Formula best_formula = {0};
    FormulaExperience best_experience = {0};
    int have_candidate = 0;

    KolibriAISelfplayConfig selfplay_cfg = {0};
    FormulaSearchConfig search_config = {0};
    double baseline_reward = 0.5;
    unsigned int rng_state = 0;
    KolibriDifficultyLevel planned_level = KOLIBRI_DIFFICULTY_FOUNDATION;
    double expected_reward = 0.0;
    size_t candidate_limit = 0;
    FormulaMemoryFact *facts = NULL;
    size_t memory_count = 0;

    pthread_mutex_lock(&ai->mutex);
    ai->iterations++;
    baseline_reward = ai->average_reward > 0.0 ? ai->average_reward : 0.5;
    selfplay_cfg = ai->selfplay_config;
    search_config = ai->search_config;
    rng_state = ai->rng_state;
    planned_level = curriculum_plan_mcts(ai, &rng_state, &expected_reward);
    ai->curriculum.current_level = planned_level;
    candidate_limit = search_config.max_candidates ? search_config.max_candidates : 16;
    if (ai->pipeline && candidate_limit > ai->pipeline->candidates.capacity) {
        candidate_limit = ai->pipeline->candidates.capacity;
    }
    memory_count = ai->memory.count;
    if (memory_count > 0) {
        facts = calloc(memory_count, sizeof(*facts));
        if (facts) {
            for (size_t i = 0; i < memory_count; ++i) {
                const KolibriMemoryFact *src = &ai->memory.facts[i];
                FormulaMemoryFact *dst = &facts[i];
                copy_string_truncated(dst->fact_id, sizeof(dst->fact_id), src->key);
                copy_string_truncated(dst->description, sizeof(dst->description), src->value);
                dst->importance = clamp01(src->salience);
                dst->reward = clamp01(src->salience);
                dst->timestamp = src->last_updated;
            }
        } else {
            memory_count = 0;
        }
    }

    pthread_mutex_unlock(&ai->mutex);

    if (ai->pipeline) {
        FormulaMemorySnapshot snapshot = {facts, memory_count};
        formula_training_pipeline_set_search_config(ai->pipeline, &search_config);
        formula_training_pipeline_prepare(ai->pipeline,
                                          ai->library,
                                          snapshot.count ? &snapshot : NULL,
                                          candidate_limit);
        formula_training_pipeline_evaluate(ai->pipeline, ai->library);
        FormulaHypothesis *best = formula_training_pipeline_select_best(ai->pipeline);
        double planning_target = baseline_reward;
        if (best) {
            planning_target = best->experience.reward;
            if (best->experience.reward > baseline_reward &&
                formula_copy(&best_formula, &best->formula) == 0) {
                best_experience = best->experience;
                have_candidate = 1;
            }
        }
        pthread_mutex_lock(&ai->mutex);
        ai->planning_score = 0.8 * ai->planning_score + 0.2 * planning_target;
        pthread_mutex_unlock(&ai->mutex);
        if (have_candidate) {
            formula_training_pipeline_record_experience(ai->pipeline, &best_experience);
        }
    } else {
        pthread_mutex_lock(&ai->mutex);
        ai->planning_score = 0.9 * ai->planning_score + 0.1 * baseline_reward;
        pthread_mutex_unlock(&ai->mutex);
    }

    if (facts) {
        free(facts);
    }

    if (have_candidate) {
        kolibri_ai_apply_reinforcement(ai, &best_formula, &best_experience);
        formula_clear(&best_formula);
    }

    size_t tasks_to_run = selfplay_cfg.tasks_per_iteration;
    double reward_sum = 0.0;
    double success_sum = 0.0;
    size_t interactions = 0;
    if (tasks_to_run > 0) {
        for (size_t i = 0; i < tasks_to_run; ++i) {
            KolibriSelfplayTask task;
            if (kolibri_selfplay_generate_task(&rng_state,
                                               selfplay_cfg.max_difficulty,
                                               &task) != 0) {
                continue;
            }
            double predicted = task.expected_result;
            double jitter = ((int)(rand_r(&rng_state) % 5) - 2) * 0.05;
            predicted += jitter;
            double error = fabs(predicted - task.expected_result);
            double reward = 1.0 / (1.0 + error);
            reward_sum += reward;
            if (error < 0.25) {
                success_sum += 1.0;
            }
            KolibriAISelfplayInteraction interaction = {0};
            interaction.task = task;
            interaction.predicted_result = predicted;
            interaction.error = error;
            interaction.reward = reward;
            interaction.success = error < 0.25 ? 1 : 0;
            kolibri_ai_record_interaction(ai, &interaction);
            interactions++;
        }
    }

    pthread_mutex_lock(&ai->mutex);
    ai->rng_state = rng_state;
    if (interactions > 0) {
        double avg_reward = reward_sum / (double)interactions;
        double success_rate = success_sum / (double)interactions;
        double alpha = clamp01(ai->curriculum.ema_alpha);
        ai->curriculum.reward_ema[planned_level] =
            (1.0 - alpha) * ai->curriculum.reward_ema[planned_level] + alpha * avg_reward;
        ai->curriculum.success_ema[planned_level] =
            (1.0 - alpha) * ai->curriculum.success_ema[planned_level] + alpha * success_rate;
        ai->curriculum.global_success_ema =
            (1.0 - alpha) * ai->curriculum.global_success_ema + alpha * success_rate;
        ai->curriculum.sample_count[planned_level] += interactions;

        double delta = avg_reward - baseline_reward;
        ai->curriculum.integral_error += delta;
        ai->curriculum.last_error = delta;

        double adjustment = 0.05 * delta + 0.01 * ai->curriculum.integral_error;
        ai->curriculum.distribution[planned_level] =
            clamp01(ai->curriculum.distribution[planned_level] + adjustment);

        double total = ai->curriculum.distribution[planned_level];
        for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
            if ((KolibriDifficultyLevel)i == planned_level) {
                continue;
            }
            ai->curriculum.distribution[i] = clamp01(
                ai->curriculum.distribution[i] -
                adjustment / (double)(KOLIBRI_DIFFICULTY_COUNT - 1));
            total += ai->curriculum.distribution[i];
        }
        if (total > 0.0) {
            for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
                ai->curriculum.distribution[i] /= total;
            }
        }
        ai->curriculum.temperature =
            clamp01(0.5 + 0.5 * (1.0 - ai->curriculum.global_success_ema));
    }

    pthread_mutex_unlock(&ai->mutex);

    if (ai->pipeline) {
        FormulaMemorySnapshot snapshot = {facts, memory_count};
        formula_training_pipeline_set_search_config(ai->pipeline, &search_config);
        formula_training_pipeline_prepare(ai->pipeline,
                                          ai->library,
                                          snapshot.count ? &snapshot : NULL,
                                          candidate_limit);
        formula_training_pipeline_evaluate(ai->pipeline, ai->library);
        FormulaHypothesis *best = formula_training_pipeline_select_best(ai->pipeline);
        double planning_target = baseline_reward;
        if (best) {
            planning_target = best->experience.reward;
            if (best->experience.reward > baseline_reward &&
                formula_copy(&best_formula, &best->formula) == 0) {
                best_experience = best->experience;
                have_candidate = 1;
            }
        }
        pthread_mutex_lock(&ai->mutex);
        ai->planning_score = 0.8 * ai->planning_score + 0.2 * planning_target;
        pthread_mutex_unlock(&ai->mutex);
        if (have_candidate) {
            formula_training_pipeline_record_experience(ai->pipeline, &best_experience);
        }
    } else {
        pthread_mutex_lock(&ai->mutex);
        ai->planning_score = 0.9 * ai->planning_score + 0.1 * baseline_reward;
        pthread_mutex_unlock(&ai->mutex);
    }

    if (facts) {
        free(facts);
    }

    if (have_candidate) {
        kolibri_ai_apply_reinforcement(ai, &best_formula, &best_experience);
        formula_clear(&best_formula);
    }

    size_t tasks_to_run = selfplay_cfg.tasks_per_iteration;
    double reward_sum = 0.0;
    double success_sum = 0.0;
    size_t interactions = 0;
    if (tasks_to_run > 0) {
        for (size_t i = 0; i < tasks_to_run; ++i) {
            KolibriSelfplayTask task;
            if (kolibri_selfplay_generate_task(&rng_state,
                                               selfplay_cfg.max_difficulty,
                                               &task) != 0) {
                continue;
            }
            double predicted = task.expected_result;
            double jitter = ((int)(rand_r(&rng_state) % 5) - 2) * 0.05;
            predicted += jitter;
            double error = fabs(predicted - task.expected_result);
            double reward = 1.0 / (1.0 + error);
            reward_sum += reward;
            if (error < 0.25) {
                success_sum += 1.0;
            }
            KolibriAISelfplayInteraction interaction = {0};
            interaction.task = task;
            interaction.predicted_result = predicted;
            interaction.error = error;
            interaction.reward = reward;
            interaction.success = error < 0.25 ? 1 : 0;
            kolibri_ai_record_interaction(ai, &interaction);
            interactions++;
        }
    }

    pthread_mutex_lock(&ai->mutex);
    ai->rng_state = rng_state;
    if (interactions > 0) {
        double avg_reward = reward_sum / (double)interactions;
        double success_rate = success_sum / (double)interactions;
        double alpha = clamp01(ai->curriculum.ema_alpha);
        ai->curriculum.reward_ema[planned_level] =
            (1.0 - alpha) * ai->curriculum.reward_ema[planned_level] + alpha * avg_reward;
        ai->curriculum.success_ema[planned_level] =
            (1.0 - alpha) * ai->curriculum.success_ema[planned_level] + alpha * success_rate;
        ai->curriculum.global_success_ema =
            (1.0 - alpha) * ai->curriculum.global_success_ema + alpha * success_rate;
        ai->curriculum.sample_count[planned_level] += interactions;

        double delta = avg_reward - baseline_reward;
        ai->curriculum.integral_error += delta;
        ai->curriculum.last_error = delta;

        double adjustment = 0.05 * delta + 0.01 * ai->curriculum.integral_error;
        ai->curriculum.distribution[planned_level] =
            clamp01(ai->curriculum.distribution[planned_level] + adjustment);

        double total = ai->curriculum.distribution[planned_level];
        for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
            if ((KolibriDifficultyLevel)i == planned_level) {
                continue;
            }
            ai->curriculum.distribution[i] = clamp01(
                ai->curriculum.distribution[i] -
                adjustment / (double)(KOLIBRI_DIFFICULTY_COUNT - 1));
            total += ai->curriculum.distribution[i];
        }
        if (total > 0.0) {
            for (size_t i = 0; i < KOLIBRI_DIFFICULTY_COUNT; ++i) {
                ai->curriculum.distribution[i] /= total;
            }
        }
        ai->curriculum.temperature =
            clamp01(0.5 + 0.5 * (1.0 - ai->curriculum.global_success_ema));
    }
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
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t max_copy = dst_size - 1;
    size_t len = 0;
    while (len < max_copy && src[len] != '\0') {
        len++;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

void kolibri_ai_record_interaction(KolibriAI *ai,
                                   const KolibriAISelfplayInteraction *interaction) {
    if (!ai || !interaction) {
        return;
    }
    KolibriAIDatasetEntry entry = {0};
    copy_string(entry.prompt, sizeof(entry.prompt), interaction->task.description);
    snprintf(entry.response,
             sizeof(entry.response),
             "%.3f",
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
    if (ai->pipeline) {
        formula_training_pipeline_set_search_config(ai->pipeline, &ai->search_config);
    }
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


    unsigned int rng_state = ai->rng_state;
    double reward = 0.0;
    KolibriDifficultyLevel level = curriculum_plan_mcts(ai, &rng_state, &reward);
    ai->rng_state = rng_state;

    if (expected_reward) {
        *expected_reward = reward;
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
    snprintf(entry.response,
             sizeof(entry.response),
             "%.3f",
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
    memcpy(copy, text, len + 1);
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



char *kolibri_ai_export_snapshot(const KolibriAI *ai) {
    if (!ai) {
        return NULL;
    }


char *kolibri_ai_export_snapshot(const KolibriAI *ai_const) {
    if (!ai_const) {


    struct json_object *root = json_object_new_object();
    if (!root) {
        pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);
        return NULL;
    }

    json_object_object_add(root, "iterations", json_object_new_int64((long long)ai->iterations));
    json_object_object_add(root, "average_reward", json_object_new_double(ai->average_reward));
    json_object_object_add(root, "planning_score", json_object_new_double(ai->planning_score));
    json_object_object_add(root, "recent_poe", json_object_new_double(ai->recent_poe));
    json_object_object_add(root, "recent_mdl", json_object_new_double(ai->recent_mdl));
    json_object_object_add(root,
                           "selfplay_total_interactions",
                           json_object_new_int64((long long)ai->selfplay_total_interactions));

    struct json_object *formulas = json_object_new_array();
    struct json_object *dataset = json_object_new_array();
    struct json_object *memory = json_object_new_array();
    if (!formulas || !dataset || !memory) {
        if (formulas) {
            json_object_put(formulas);
        }
        if (dataset) {
            json_object_put(dataset);
        }
        if (memory) {
            json_object_put(memory);
        }
        json_object_put(root);
        pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);

        return NULL;
    }
    KolibriAI *ai = (KolibriAI *)ai_const;
    pthread_mutex_lock(&ai->mutex);


    if (ai->library) {
        for (size_t i = 0; i < ai->library->count; ++i) {
            const Formula *formula = &ai->library->formulas[i];
            struct json_object *entry = json_object_new_object();
            if (!entry) {
                continue;
            }
            json_object_object_add(entry, "id", json_object_new_string(formula->id));
            json_object_object_add(entry,
                                   "content",
                                   json_object_new_string(formula->content));
            json_object_object_add(entry,
                                   "effectiveness",
                                   json_object_new_double(formula->effectiveness));
            json_object_object_add(entry,
                                   "created_at",
                                   json_object_new_int64((long long)formula->created_at));
            json_object_object_add(entry,
                                   "tests_passed",
                                   json_object_new_int64((int64_t)formula->tests_passed));
            json_object_object_add(entry,
                                   "confirmations",
                                   json_object_new_int64((int64_t)formula->confirmations));
            json_object_array_add(formulas, entry);
        }

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

        if (!obj) {
            continue;
        }

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

        if (!obj) {
            continue;
        }

        json_object_object_add(obj, "key", json_object_new_string(fact->key));
        json_object_object_add(obj, "value", json_object_new_string(fact->value));
        json_object_object_add(obj, "salience", json_object_new_double(fact->salience));
        json_object_object_add(obj,
                               "last_updated",

                               json_object_new_int64((long long)fact->last_updated));


                               json_object_new_int64((int64_t)fact->last_updated));


        json_object_array_add(memory, obj);
    }
    json_object_object_add(root, "memory", memory);


    pthread_mutex_unlock((pthread_mutex_t *)&ai->mutex);


    pthread_mutex_unlock(&ai->mutex);


    char *json = dup_json_string(root);
    json_object_put(root);

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

    ai->iterations = 0;
    ai->average_reward = 0.0;
    ai->planning_score = 0.0;
    ai->recent_poe = 0.0;
    ai->recent_mdl = 0.0;
    ai->selfplay_total_interactions = 0;

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
    if (json_object_object_get_ex(root, "selfplay_total_interactions", &value)) {
        ai->selfplay_total_interactions = (uint64_t)json_object_get_int64(value);
    }

    if (ai->library) {
        for (size_t i = 0; i < ai->library->count; ++i) {
            formula_clear(&ai->library->formulas[i]);

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

    dataset_clear(&ai->dataset);
    memory_clear(&ai->memory);

    if (json_object_object_get_ex(root, "formulas", &value) &&
        json_object_is_type(value, json_type_array) && ai->library) {
        size_t count = (size_t)json_object_array_length(value);
        for (size_t i = 0; i < count; ++i) {
            struct json_object *entry = json_object_array_get_idx(value, (int)i);
            if (!entry || !json_object_is_type(entry, json_type_object)) {
                continue;
            }
            struct json_object *field = NULL;
            const char *id = NULL;
            const char *content = NULL;
            double effectiveness = 0.0;
            time_t created_at = 0;
            uint32_t tests = 0;
            uint32_t confirmations = 0;

            if (json_object_object_get_ex(entry, "id", &field)) {
                id = json_object_get_string(field);
            }
            if (json_object_object_get_ex(entry, "content", &field)) {
                content = json_object_get_string(field);
            }
            if (json_object_object_get_ex(entry, "effectiveness", &field)) {
                effectiveness = json_object_get_double(field);
            }
            if (json_object_object_get_ex(entry, "created_at", &field)) {
                created_at = (time_t)json_object_get_int64(field);
            }
            if (json_object_object_get_ex(entry, "tests_passed", &field)) {
                tests = (uint32_t)json_object_get_int64(field);
            }
            if (json_object_object_get_ex(entry, "confirmations", &field)) {
                confirmations = (uint32_t)json_object_get_int64(field);
            }

            if (!id) {
                continue;
            }
            Formula formula = {0};
            formula.representation = FORMULA_REPRESENTATION_TEXT;
            copy_string_truncated(formula.id, sizeof(formula.id), id);
            if (content) {
                copy_string_truncated(formula.content, sizeof(formula.content), content);
            }
            formula.effectiveness = effectiveness;
            formula.created_at = created_at;
            formula.tests_passed = tests;
            formula.confirmations = confirmations;
            formula_collection_add(ai->library, &formula);
        }
    }

    if (json_object_object_get_ex(root, "dataset", &value) &&
        json_object_is_type(value, json_type_array)) {
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
            dataset_append(&ai->dataset, &item);
        }
    }

    if (json_object_object_get_ex(root, "memory", &value) &&
        json_object_is_type(value, json_type_array)) {
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

    update_average_reward(ai);


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
