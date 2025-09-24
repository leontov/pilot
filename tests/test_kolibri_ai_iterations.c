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
    KolibriAI *ai = kolibri_ai_create();
    assert(ai != NULL);

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
    return 0;
}
