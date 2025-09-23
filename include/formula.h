#ifndef FORMULA_H
#define FORMULA_H

#include "formula_core.h"
#include "synthesis/search.h"

// High level categories used by legacy evaluators.
extern const int FORMULA_TYPE_SIMPLE;
extern const int FORMULA_TYPE_POLYNOMIAL;
extern const int FORMULA_TYPE_COMPOSITE;
extern const int FORMULA_TYPE_PERIODIC;

// Lifecycle helpers for working with the unified structure.
void formula_clear(Formula* formula);
int formula_copy(Formula* dest, const Formula* src);

// Collection helpers used by the Kolibri AI subsystem.
FormulaCollection* formula_collection_create(size_t initial_capacity);
void formula_collection_destroy(FormulaCollection* collection);
int formula_collection_add(FormulaCollection* collection, const Formula* formula);
Formula* formula_collection_find(FormulaCollection* collection, const char* id);
size_t formula_collection_get_top(const FormulaCollection* collection,
                                  const Formula** out_formulas,
                                  size_t max_results);

// Text-based formula utilities.
int get_formula_type(const char* content);

int validate_formula(const Formula* formula);
char* serialize_formula(const Formula* formula);
Formula* deserialize_formula(const char* json);

void formula_collection_remove(FormulaCollection* collection, const char* id);


// --- Новая подсистема обучения формул ---

typedef struct {
    char fact_id[64];
    char description[256];
    double importance;
    double reward;
    time_t timestamp;
} FormulaMemoryFact;

typedef struct {
    FormulaMemoryFact* facts;
    size_t count;
} FormulaMemorySnapshot;

typedef struct {
    char task[256];
    char response[512];
    double effectiveness;
    int rating;
    time_t timestamp;
} FormulaDatasetEntry;

typedef struct {
    FormulaDatasetEntry* entries;
    size_t count;
} FormulaDataset;

typedef struct {
    double reward;
    double imitation_score;
    double accuracy;
    double loss;
    char source[128];
    char task_id[64];
} FormulaExperience;

typedef struct {
    Formula formula;
    FormulaExperience experience;
} FormulaHypothesis;

typedef struct {
    FormulaHypothesis* hypotheses;
    size_t count;
    size_t capacity;
} FormulaHypothesisBatch;

typedef struct {
    double average_reward;
    double average_imitation;
    double success_rate;
    size_t total_evaluated;
} FormulaTrainingMetrics;

typedef struct {
    FormulaHypothesisBatch candidates;
    FormulaTrainingMetrics metrics;
    FormulaDataset dataset;
    FormulaMemorySnapshot memory_snapshot;
    unsigned char* weights;
    size_t weights_size;
    FormulaSearchConfig search_config;
} FormulaTrainingPipeline;

FormulaMemorySnapshot formula_memory_snapshot_clone(const FormulaMemoryFact* facts,
                                                   size_t count);
void formula_memory_snapshot_release(FormulaMemorySnapshot* snapshot);

FormulaTrainingPipeline* formula_training_pipeline_create(size_t capacity);
void formula_training_pipeline_destroy(FormulaTrainingPipeline* pipeline);
int formula_training_pipeline_load_dataset(FormulaTrainingPipeline* pipeline,
                                          const char* path);
int formula_training_pipeline_load_weights(FormulaTrainingPipeline* pipeline,
                                          const char* path);
void formula_training_pipeline_set_search_config(FormulaTrainingPipeline* pipeline,
                                                 const FormulaSearchConfig* config);
int formula_training_pipeline_prepare(FormulaTrainingPipeline* pipeline,
                                      const FormulaCollection* library,
                                      const FormulaMemorySnapshot* snapshot,
                                      size_t max_candidates);
int formula_training_pipeline_evaluate(FormulaTrainingPipeline* pipeline,
                                       FormulaCollection* library);
FormulaHypothesis* formula_training_pipeline_select_best(FormulaTrainingPipeline* pipeline);
int formula_training_pipeline_record_experience(FormulaTrainingPipeline* pipeline,
                                               const FormulaExperience* experience);


#endif // FORMULA_H
