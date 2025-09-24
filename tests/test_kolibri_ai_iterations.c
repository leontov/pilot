/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "kolibri_ai.h"
#include "formula.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EPSILON 1e-6

FormulaTrainingPipeline *formula_training_pipeline_create(size_t capacity) {
    FormulaTrainingPipeline *pipeline = calloc(1, sizeof(FormulaTrainingPipeline));
    if (!pipeline) {
        return NULL;
    }
    pipeline->candidates.capacity = capacity;
    pipeline->metrics.average_reward = 0.0;
    pipeline->metrics.average_imitation = 0.0;
    pipeline->metrics.success_rate = 0.0;
    pipeline->metrics.total_evaluated = 0;
    return pipeline;
}

void formula_training_pipeline_destroy(FormulaTrainingPipeline *pipeline) {
    if (!pipeline) {
        return;
    }
    free(pipeline);
}

int formula_training_pipeline_record_experience(FormulaTrainingPipeline *pipeline,
                                               const FormulaExperience *experience) {
    if (!pipeline || !experience) {
        return -1;
    }

    size_t previous = pipeline->metrics.total_evaluated;
    double total_reward = pipeline->metrics.average_reward * (double)previous + experience->reward;
    double total_imitation = pipeline->metrics.average_imitation * (double)previous +
                             experience->imitation_score;
    double success_contribution = experience->reward > 0.2 ? 1.0 : experience->reward;
    double total_success = pipeline->metrics.success_rate * (double)previous + success_contribution;

    pipeline->metrics.total_evaluated = previous + 1;
    double denom = (double)pipeline->metrics.total_evaluated;
    pipeline->metrics.average_reward = total_reward / denom;
    pipeline->metrics.average_imitation = total_imitation / denom;
    pipeline->metrics.success_rate = total_success / denom;
    return 0;
}

typedef struct {
    double rewards[3];
    double effectiveness[3];
    size_t total;
} MockIterationPlan;

typedef struct {
    size_t total_attempts;
    size_t accepted_blocks;
} MockBlockchain;

typedef struct {
    double average_reward;
    size_t formula_count;
    size_t queue_depth;
    size_t dataset_size;
    double curriculum_temperature;
} AISnapshot;

static double parse_json_double(const char *json, const char *needle) {
    const char *pos = strstr(json, needle);
    assert(pos && "missing field");
    pos += strlen(needle);
    return strtod(pos, NULL);
}

static size_t parse_json_size(const char *json, const char *needle) {
    const char *pos = strstr(json, needle);
    assert(pos && "missing field");
    pos += strlen(needle);
    return (size_t)strtoull(pos, NULL, 10);
}

static AISnapshot capture_ai_snapshot(KolibriAI *ai) {
    AISnapshot snapshot = {0};
    char *state = kolibri_ai_serialize_state(ai);
    assert(state != NULL);
    snapshot.average_reward =
        parse_json_double(state, "\"average_reward\":");
    snapshot.formula_count =
        parse_json_size(state, "\"formula_count\":");
    snapshot.queue_depth =
        parse_json_size(state, "\"queue_depth\":");
    snapshot.dataset_size =
        parse_json_size(state, "\"dataset_size\":");
    snapshot.curriculum_temperature =
        parse_json_double(state, "\"curriculum_temperature\":");
    free(state);
    return snapshot;
}

static FormulaExperience create_mock_experience(const MockIterationPlan *plan,
                                                size_t index,
                                                MockBlockchain *chain) {
    assert(plan != NULL);
    assert(index < plan->total);
    assert(chain != NULL);

    FormulaExperience experience;
    memset(&experience, 0, sizeof(experience));

    experience.reward = plan->rewards[index];
    experience.imitation_score = 0.05 * (double)(index + 1);
    experience.accuracy = 0.1 * (double)(index + 1);
    experience.loss = fmax(0.0, 1.0 - experience.reward);
    snprintf(experience.source, sizeof(experience.source), "self-play");
    snprintf(experience.task_id, sizeof(experience.task_id),
             "mock-task-%zu", index);

    chain->total_attempts++;
    if (experience.reward >= 0.25) {
        chain->accepted_blocks++;
    }

    return experience;
}

static void add_mock_formula(KolibriAI *ai,
                             const MockIterationPlan *plan,
                             size_t index) {
    assert(ai != NULL);
    assert(plan != NULL);
    assert(index < plan->total);

    Formula formula;
    memset(&formula, 0, sizeof(formula));
    formula.representation = FORMULA_REPRESENTATION_TEXT;
    snprintf(formula.id, sizeof(formula.id), "mock.synthetic.%zu", index);
    snprintf(formula.content, sizeof(formula.content),
             "h_%zu(x) = %.1fx + 1", index, 2.0 + (double)index);
    formula.effectiveness = plan->effectiveness[index];
    formula.created_at = (time_t)(1700000000 + (time_t)index);
    formula.tests_passed = 1;
    formula.confirmations = 1;

    assert(kolibri_ai_add_formula(ai, &formula) == 0);
}

int main(void) {
    KolibriAI *ai = kolibri_ai_create(NULL);
    assert(ai != NULL);

    kolibri_config_t quiet_cfg = {0};
    quiet_cfg.search = formula_search_config_default();
    quiet_cfg.search.max_candidates = 0;
    quiet_cfg.selfplay.tasks_per_iteration = 0;
    quiet_cfg.selfplay.max_difficulty = 0;
    quiet_cfg.ai.snapshot_limit = 128;
    kolibri_ai_apply_config(ai, &quiet_cfg);

    FormulaTrainingPipeline *pipeline = formula_training_pipeline_create(4);
    assert(pipeline != NULL);

    MockIterationPlan plan = {
        .rewards = {0.05, 0.35, 0.6},
        .effectiveness = {0.66, 0.72, 0.78},
        .total = 3,
    };
    MockBlockchain chain = {0, 0};

    AISnapshot baseline = capture_ai_snapshot(ai);
    double previous_ai_average = baseline.average_reward;
    size_t previous_formula_count = baseline.formula_count;
    assert(baseline.curriculum_temperature > 0.0);
    assert(baseline.dataset_size <= quiet_cfg.ai.snapshot_limit);
    assert(baseline.queue_depth <= quiet_cfg.selfplay.tasks_per_iteration +
                                      quiet_cfg.search.max_candidates + 8);
    double previous_pipeline_average = 0.0;
    double previous_success_rate = 0.0;

    for (size_t i = 0; i < plan.total; ++i) {
        kolibri_ai_process_iteration(ai);

        FormulaExperience experience = create_mock_experience(&plan, i, &chain);
        assert(formula_training_pipeline_record_experience(pipeline, &experience) == 0);

        add_mock_formula(ai, &plan, i);

        AISnapshot snapshot = capture_ai_snapshot(ai);

        assert(snapshot.formula_count == previous_formula_count + 1);
        assert(snapshot.average_reward - previous_ai_average > EPSILON);
        assert(snapshot.curriculum_temperature > 0.0);
        assert(snapshot.dataset_size <= quiet_cfg.ai.snapshot_limit);
        assert(pipeline->metrics.average_reward - previous_pipeline_average > EPSILON);
        assert(pipeline->metrics.success_rate - previous_success_rate > EPSILON);

        previous_ai_average = snapshot.average_reward;
        previous_formula_count = snapshot.formula_count;
        previous_pipeline_average = pipeline->metrics.average_reward;
        previous_success_rate = pipeline->metrics.success_rate;
    }

    assert(chain.total_attempts == plan.total);
    assert(chain.accepted_blocks == 2);
    assert(pipeline->metrics.total_evaluated == plan.total);

    formula_training_pipeline_destroy(pipeline);
    kolibri_ai_destroy(ai);

    kolibri_config_t auto_cfg = {0};
    auto_cfg.search = formula_search_config_default();
    auto_cfg.search.max_candidates = 3;
    auto_cfg.selfplay.tasks_per_iteration = 4;
    auto_cfg.selfplay.max_difficulty = 3;
    auto_cfg.ai.snapshot_limit = 64;
    auto_cfg.seed = 1337;

    KolibriAI *auto_ai = kolibri_ai_create(&auto_cfg);
    assert(auto_ai != NULL);

    char *baseline_state = kolibri_ai_serialize_state(auto_ai);
    assert(baseline_state != NULL);
    size_t baseline_formulas =
        parse_json_size(baseline_state, "\"formula_count\":");
    double baseline_reward =
        parse_json_double(baseline_state, "\"average_reward\":");
    free(baseline_state);

    for (size_t iter = 0; iter < 12; ++iter) {
        kolibri_ai_process_iteration(auto_ai);
    }

    char *post_state = kolibri_ai_serialize_state(auto_ai);
    assert(post_state != NULL);
    size_t evolved_formulas =
        parse_json_size(post_state, "\"formula_count\":");
    double evolved_reward =
        parse_json_double(post_state, "\"average_reward\":");
    size_t evolved_queue =
        parse_json_size(post_state, "\"queue_depth\":");
    double evolved_temp =
        parse_json_double(post_state, "\"curriculum_temperature\":");
    free(post_state);

    assert(evolved_formulas >= baseline_formulas);
    assert(evolved_reward + EPSILON >= baseline_reward);
    assert(evolved_queue < 32);
    assert(evolved_temp > 0.0 && evolved_temp < 2.0);

    KolibriAISelfplayInteraction log_buffer[128];
    size_t logged = kolibri_ai_get_interaction_log(auto_ai, log_buffer, 128);
    assert(logged > 3);

    size_t split = logged / 2;
    if (split == 0) {
        split = 1;
    }
    size_t tail = logged - split;
    if (tail == 0) {
        tail = 1;
    }

    double first_avg = 0.0;
    for (size_t i = 0; i < split; ++i) {
        first_avg += log_buffer[i].reward;
    }
    first_avg /= (double)split;

    double second_avg = 0.0;
    for (size_t i = split; i < logged; ++i) {
        second_avg += log_buffer[i].reward;
    }
    second_avg /= (double)tail;
    assert(second_avg + 1e-6 >= first_avg);

    double log_avg = 0.0;
    double max_reward = 0.0;
    for (size_t i = 0; i < logged; ++i) {
        log_avg += log_buffer[i].reward;
        if (log_buffer[i].reward > max_reward) {
            max_reward = log_buffer[i].reward;
        }
    }
    log_avg /= (double)logged;
    assert(max_reward + 1e-6 >= 0.6);

    double max_error = 0.0;
    double replay_avg = 0.0;
    assert(kolibri_ai_replay_log(auto_ai, &max_error, &replay_avg) == 0);
    assert(max_error < 1e-6);
    assert(fabs(replay_avg - log_avg) < 1e-6);

    char *snapshot = kolibri_ai_export_snapshot(auto_ai);
    assert(snapshot != NULL);
    KolibriAI *replica = kolibri_ai_create(&auto_cfg);
    assert(replica != NULL);
    assert(kolibri_ai_import_snapshot(replica, snapshot) == 0);
    char *roundtrip = kolibri_ai_export_snapshot(replica);
    assert(roundtrip != NULL);
    assert(strcmp(snapshot, roundtrip) == 0);
    free(snapshot);
    free(roundtrip);
    kolibri_ai_destroy(replica);
    kolibri_ai_destroy(auto_ai);
    return 0;
}
