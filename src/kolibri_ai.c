#include "kolibri_ai.h"
#include "formula.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <uuid/uuid.h>

static int kolibri_read_file_bytes(const char* path, unsigned char** buffer, size_t* size) {
    if (!path || !buffer || !size) {
        return -1;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        return -1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }

    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return -1;
    }
    rewind(file);

    unsigned char* data = malloc((size_t)file_size);
    if (!data) {
        fclose(file);
        return -1;
    }

    size_t read = fread(data, 1, (size_t)file_size, file);
    fclose(file);
    if (read != (size_t)file_size) {
        free(data);
        return -1;
    }

    *buffer = data;
    *size = (size_t)file_size;
    return 0;
}

typedef struct {
    FormulaHypothesis* best_hypothesis;
    double planning_score;
} KolibriPlanningResult;

static KolibriMemoryModule* kolibri_memory_create(size_t capacity) {
    KolibriMemoryModule* memory = calloc(1, sizeof(KolibriMemoryModule));
    if (!memory) {
        return NULL;
    }

    memory->entries = calloc(capacity, sizeof(KolibriMemoryEntry));
    if (!memory->entries) {
        free(memory);
        return NULL;
    }

    memory->capacity = capacity;
    memory->count = 0;
    return memory;
}

static void kolibri_memory_destroy(KolibriMemoryModule* memory) {
    if (!memory) {
        return;
    }
    free(memory->entries);
    free(memory);
}

static void kolibri_memory_shift(KolibriMemoryModule* memory) {
    if (!memory || memory->count < memory->capacity) {
        return;
    }

    memmove(&memory->entries[0], &memory->entries[1],
            sizeof(KolibriMemoryEntry) * (memory->capacity - 1));
    memory->count = memory->capacity - 1;
}

static void kolibri_memory_store(KolibriMemoryModule* memory,
                                 KolibriMemoryEntryType type,
                                 const char* description,
                                 const char* source,
                                 double reward,
                                 double importance) {
    if (!memory || !description) {
        return;
    }

    if (memory->count >= memory->capacity) {
        kolibri_memory_shift(memory);
    }

    KolibriMemoryEntry* entry = &memory->entries[memory->count++];
    memset(entry, 0, sizeof(*entry));
    entry->type = type;
    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse(uuid, entry->id);
    strncpy(entry->description, description, sizeof(entry->description) - 1);
    if (source) {
        strncpy(entry->source, source, sizeof(entry->source) - 1);
    }
    entry->importance = importance;
    entry->reward = reward;
    entry->timestamp = time(NULL);
}

static void kolibri_memory_apply_reward(KolibriMemoryModule* memory,
                                        const char* description,
                                        double reward) {
    if (!memory || !description) {
        return;
    }

    for (size_t i = 0; i < memory->count; ++i) {
        KolibriMemoryEntry* entry = &memory->entries[i];
        if (strstr(description, entry->description) != NULL) {
            entry->reward = (entry->reward + reward) / 2.0;
            entry->importance = fmin(1.0, entry->importance + fabs(reward) * 0.1);
        } else {
            entry->importance = fmax(0.05, entry->importance * 0.95);
        }
    }
}

static FormulaMemorySnapshot kolibri_memory_snapshot(const KolibriMemoryModule* memory,
                                                     size_t max_entries) {
    FormulaMemorySnapshot snapshot = {0};
    if (!memory || memory->count == 0) {
        return snapshot;
    }

    size_t count = memory->count < max_entries ? memory->count : max_entries;
    FormulaMemoryFact* facts = calloc(count, sizeof(FormulaMemoryFact));
    if (!facts) {
        return snapshot;
    }

    for (size_t i = 0; i < count; ++i) {
        const KolibriMemoryEntry* entry = &memory->entries[memory->count - 1 - i];
        FormulaMemoryFact* fact = &facts[i];
        strncpy(fact->fact_id, entry->id, sizeof(fact->fact_id) - 1);
        strncpy(fact->description, entry->description, sizeof(fact->description) - 1);
        fact->importance = entry->importance;
        fact->reward = entry->reward;
        fact->timestamp = entry->timestamp;
    }

    snapshot.facts = facts;
    snapshot.count = count;
    return snapshot;
}

static void sensor_state_init(KolibriSensorState* state, size_t capacity) {
    if (!state) {
        return;
    }

    state->readings = calloc(capacity, sizeof(KolibriSensorReading));
    state->capacity = capacity;
    state->count = 0;
}

static void sensor_state_destroy(KolibriSensorState* state) {
    if (!state) {
        return;
    }

    free(state->readings);
    state->readings = NULL;
    state->count = 0;
    state->capacity = 0;
}

static void sensor_state_record(KolibriSensorState* state,
                                const char* modality,
                                double value,
                                double confidence) {
    if (!state || !modality) {
        return;
    }

    for (size_t i = 0; i < state->count; ++i) {
        KolibriSensorReading* reading = &state->readings[i];
        if (strcmp(reading->modality, modality) == 0) {
            reading->value = value;
            reading->confidence = confidence;
            reading->timestamp = time(NULL);
            return;
        }
    }

    if (state->count >= state->capacity) {
        size_t new_capacity = state->capacity > 0 ? state->capacity * 2 : 4;
        KolibriSensorReading* resized = realloc(state->readings,
                                                new_capacity * sizeof(KolibriSensorReading));
        if (!resized) {
            return;
        }
        state->readings = resized;
        state->capacity = new_capacity;
    }

    KolibriSensorReading* reading = &state->readings[state->count++];
    memset(reading, 0, sizeof(*reading));
    strncpy(reading->modality, modality, sizeof(reading->modality) - 1);
    reading->value = value;
    reading->confidence = confidence;
    reading->timestamp = time(NULL);
}

static void goal_set_init(KolibriGoalSet* goals) {
    if (!goals) {
        return;
    }

    goals->items = calloc(4, sizeof(KolibriGoal));
    if (!goals->items) {
        goals->capacity = 0;
        goals->count = 0;
        return;
    }

    goals->capacity = 4;
    goals->count = 2;

    KolibriGoal* quality = &goals->items[0];
    strncpy(quality->id, "solution_quality", sizeof(quality->id) - 1);
    strncpy(quality->description, "Повышать эффективность формул", sizeof(quality->description) - 1);
    quality->priority = 1.0;
    quality->target_value = 0.75;
    quality->tolerance = 0.05;

    KolibriGoal* diversity = &goals->items[1];
    strncpy(diversity->id, "knowledge_diversity", sizeof(diversity->id) - 1);
    strncpy(diversity->description, "Поддерживать разнообразие опыта", sizeof(diversity->description) - 1);
    diversity->priority = 0.6;
    diversity->target_value = 0.5;
    diversity->tolerance = 0.1;
}

static void goal_set_destroy(KolibriGoalSet* goals) {
    if (!goals) {
        return;
    }

    free(goals->items);
    goals->items = NULL;
    goals->count = 0;
    goals->capacity = 0;
}

static double goal_weight(const KolibriGoalSet* goals, const char* goal_id) {
    if (!goals || !goal_id) {
        return 0.0;
    }

    for (size_t i = 0; i < goals->count; ++i) {
        if (strcmp(goals->items[i].id, goal_id) == 0) {
            return goals->items[i].priority;
        }
    }
    return 0.0;
}

static MLPModel* mlp_model_create(const char* weights_path, double learning_rate) {
    MLPModel* model = calloc(1, sizeof(MLPModel));
    if (!model) {
        return NULL;
    }

    unsigned char* raw = NULL;
    size_t size = 0;
    if (kolibri_read_file_bytes(weights_path, &raw, &size) != 0 || size == 0) {
        free(model);
        free(raw);
        return NULL;
    }

    size_t count = size / sizeof(double);
    model->parameters = calloc(count, sizeof(double));
    if (!model->parameters) {
        free(model);
        free(raw);
        return NULL;
    }

    memcpy(model->parameters, raw, count * sizeof(double) > size ? size : count * sizeof(double));
    model->parameter_count = count;
    model->learning_rate = learning_rate;
    free(raw);
    return model;
}

static void mlp_model_destroy(MLPModel* model) {
    if (!model) {
        return;
    }
    free(model->parameters);
    free(model);
}

static double mlp_model_forward(const MLPModel* model,
                                const double* input,
                                size_t input_size) {
    if (!model || !model->parameters || !input || input_size == 0) {
        return 0.0;
    }

    size_t limit = input_size < model->parameter_count ? input_size : model->parameter_count;
    double sum = 0.0;
    for (size_t i = 0; i < limit; ++i) {
        sum += input[i] * model->parameters[i];
    }
    return tanh(sum);
}

static void mlp_model_apply_feedback(MLPModel* model, double reward) {
    if (!model || !model->parameters || model->parameter_count == 0) {
        return;
    }
    double adjustment = model->learning_rate * reward;
    for (size_t i = 0; i < model->parameter_count; ++i) {
        model->parameters[i] += adjustment / (double)(i + 1);
    }
}

static TransformerModel* transformer_model_create(const char* weights_path) {
    TransformerModel* model = calloc(1, sizeof(TransformerModel));
    if (!model) {
        return NULL;
    }

    unsigned char* raw = NULL;
    size_t size = 0;
    if (kolibri_read_file_bytes(weights_path, &raw, &size) != 0 || size == 0) {
        free(model);
        free(raw);
        return NULL;
    }

    model->parameters = calloc(size, sizeof(unsigned char));
    if (!model->parameters) {
        free(model);
        free(raw);
        return NULL;
    }

    memcpy(model->parameters, raw, size);
    model->parameter_count = size;
    model->head_count = 4;
    model->dropout = 0.1;
    free(raw);
    return model;
}

static void transformer_model_destroy(TransformerModel* model) {
    if (!model) {
        return;
    }
    free(model->parameters);
    free(model);
}

static double transformer_model_score(const TransformerModel* model,
                                      const double* context,
                                      size_t context_size) {
    if (!model || !model->parameters || !context || context_size == 0) {
        return 0.0;
    }

    double accumulator = 0.0;
    for (size_t i = 0; i < context_size; ++i) {
        accumulator += sin(context[i]) + cos(context[i] / (double)(i + 1));
    }

    double parameter_factor = model->parameter_count > 0
        ? (double)(model->parameters[0] + model->parameters[model->parameter_count / 2]) /
              (255.0 * 2.0)
        : 0.0;
    return tanh(accumulator / (double)context_size + parameter_factor);
}

static void kolibri_ai_collect_observations(KolibriAI* ai) {
    if (!ai || !ai->pipeline) {
        return;
    }

    double avg_effectiveness = 0.0;
    double avg_rating = 0.0;
    size_t count = ai->pipeline->dataset.count;
    if (count > 0) {
        for (size_t i = 0; i < count; ++i) {
            const FormulaDatasetEntry* entry = &ai->pipeline->dataset.entries[i];
            avg_effectiveness += entry->effectiveness;
            avg_rating += entry->rating;
        }
        avg_effectiveness /= (double)count;
        avg_rating /= (double)count;
    }

    sensor_state_record(&ai->sensor_state, "dataset_avg_effectiveness", avg_effectiveness, 0.85);
    sensor_state_record(&ai->sensor_state, "dataset_avg_rating", avg_rating, 0.6);
    sensor_state_record(&ai->sensor_state, "pipeline_success_rate",
                        ai->pipeline->metrics.success_rate, 0.7);
}

static void kolibri_ai_update_memory_from_sensors(KolibriAI* ai) {
    if (!ai || !ai->memory) {
        return;
    }

    for (size_t i = 0; i < ai->sensor_state.count; ++i) {
        KolibriSensorReading* reading = &ai->sensor_state.readings[i];
        char description[256];
        snprintf(description, sizeof(description), "%s=%.4f", reading->modality, reading->value);
        kolibri_memory_store(ai->memory, KOLIBRI_MEMORY_FACT, description, "sensors",
                              reading->value, reading->confidence);
    }
}

static KolibriPlanningResult kolibri_ai_plan_actions(KolibriAI* ai) {
    KolibriPlanningResult result = {0};
    if (!ai || !ai->pipeline) {
        return result;
    }

    FormulaMemorySnapshot snapshot = kolibri_memory_snapshot(ai->memory, 24);
    formula_training_pipeline_prepare(ai->pipeline, ai->formulas, &snapshot, 12);
    formula_training_pipeline_evaluate(ai->pipeline, ai->formulas);
    result.best_hypothesis = formula_training_pipeline_select_best(ai->pipeline);

    if (result.best_hypothesis) {
        double inputs[4] = {
            result.best_hypothesis->experience.reward,
            result.best_hypothesis->experience.imitation_score,
            ai->pipeline->metrics.average_reward,
            ai->pipeline->metrics.average_imitation
        };
        double mlp_score = ai->policy_mlp
                               ? mlp_model_forward(ai->policy_mlp, inputs, sizeof(inputs) / sizeof(double))
                               : 0.0;

        double context[3] = {
            (double)ai->complexity_level,
            ai->learning_rate,
            ai->pipeline->metrics.success_rate
        };
        double transformer_score = ai->world_model
                                        ? transformer_model_score(ai->world_model, context,
                                                                  sizeof(context) / sizeof(double))
                                        : 0.0;

        double goal_weight_quality = goal_weight(&ai->goals, "solution_quality");
        result.planning_score =
            (mlp_score + transformer_score) / 2.0 +
            goal_weight_quality * result.best_hypothesis->experience.reward;
    }

    formula_memory_snapshot_release(&snapshot);
    return result;
}

static void kolibri_ai_apply_reinforcement(KolibriAI* ai, const KolibriPlanningResult* result) {
    if (!ai) {
        return;
    }

    if (!result || !result->best_hypothesis) {
        memset(&ai->last_experience, 0, sizeof(ai->last_experience));
        ai->last_plan_score = 0.0;
        return;
    }

    FormulaHypothesis* best = result->best_hypothesis;
    ai->last_experience = best->experience;
    ai->last_plan_score = result->planning_score;

    const char* descriptor = best->experience.task_id[0]
                                 ? best->experience.task_id
                                 : best->formula.content;
    kolibri_memory_apply_reward(ai->memory, descriptor, best->experience.reward);

    if (ai->pipeline) {
        formula_training_pipeline_record_experience(ai->pipeline, &best->experience);
    }

    if (ai->policy_mlp) {
        mlp_model_apply_feedback(ai->policy_mlp,
                                 best->experience.reward - best->experience.loss);
    }

    if (best->experience.reward > 0.4) {
        kolibri_memory_store(ai->memory, KOLIBRI_MEMORY_EPISODE,
                             best->formula.content, "planner",
                             best->experience.reward,
                             fmin(1.0, fabs(best->experience.reward)));
    }

    if (ai->formulas && best->formula.effectiveness > 0.45) {
        formula_collection_add(ai->formulas, &best->formula);
        if (ai->formulas->count % 10 == 0) {
            kovian_chain_add_block(ai->blockchain,
                                   &ai->formulas->formulas[ai->formulas->count - 10],
                                   10);
        }
    }

    if (best->experience.reward > ai->goals.items[0].target_value) {
        ai->complexity_level++;
    }

    ai->learning_rate = fmax(0.01, ai->learning_rate * 0.999);
}

static double kolibri_ai_text_overlap(const char* a, const char* b) {
    if (!a || !b) {
        return 0.0;
    }

    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    if (len_a == 0 || len_b == 0) {
        return 0.0;
    }

    size_t min_len = len_a < len_b ? len_a : len_b;
    size_t max_len = len_a > len_b ? len_a : len_b;
    size_t match = 0;
    for (size_t i = 0; i < min_len; ++i) {
        if (tolower((unsigned char)a[i]) == tolower((unsigned char)b[i])) {
            match++;
        }
    }
    return (double)match / (double)max_len;
}

static double kolibri_ai_memory_alignment(const KolibriAI* ai, const Formula* formula) {
    if (!ai || !ai->memory || !formula) {
        return 0.0;
    }

    double total = 0.0;
    for (size_t i = 0; i < ai->memory->count; ++i) {
        const KolibriMemoryEntry* entry = &ai->memory->entries[i];
        total += kolibri_ai_text_overlap(formula->content, entry->description) * entry->importance;
    }

    if (ai->memory->count == 0) {
        return 0.0;
    }
    return total / (double)ai->memory->count;
}

// Поток AI-обработки
static void* ai_thread_function(void* arg) {
    KolibriAI* ai = (KolibriAI*)arg;
    
    while (ai->running) {
        kolibri_ai_process_iteration(ai);
        usleep(100000); // 100ms между итерациями
    }
    
    return NULL;
}

// Создание AI-подсистемы
KolibriAI* kolibri_ai_create(void) {
    KolibriAI* ai = malloc(sizeof(KolibriAI));
    if (!ai) return NULL;

    ai->formulas = formula_collection_create(1000);
    ai->blockchain = kovian_chain_create();
    pthread_mutex_init(&ai->mutex, NULL);
    ai->running = 0;

    // Начальные параметры
    ai->complexity_level = 1;
    ai->learning_rate = 0.1;
    ai->iterations = 0;

    ai->memory = kolibri_memory_create(256);
    sensor_state_init(&ai->sensor_state, 8);
    goal_set_init(&ai->goals);
    ai->pipeline = formula_training_pipeline_create(16);
    ai->policy_mlp = mlp_model_create("mlp_weights.bin", ai->learning_rate);
    ai->world_model = transformer_model_create("mlp_weights.bin");
    memset(&ai->last_experience, 0, sizeof(ai->last_experience));
    ai->last_plan_score = 0.0;

    if (ai->pipeline) {
        if (formula_training_pipeline_load_dataset(ai->pipeline, "learning_data.json") != 0) {
            formula_training_pipeline_load_dataset(ai->pipeline, "../learning_data.json");
        }
        if (formula_training_pipeline_load_weights(ai->pipeline, "mlp_weights.bin") != 0) {
            formula_training_pipeline_load_weights(ai->pipeline, "../mlp_weights.bin");
        }
    }

    if (!ai->formulas || !ai->blockchain || !ai->memory || !ai->pipeline) {
        kolibri_ai_destroy(ai);
        return NULL;
    }

    return ai;
}

// Уничтожение AI-подсистемы
void kolibri_ai_destroy(KolibriAI* ai) {
    if (!ai) return;

    kolibri_ai_stop(ai);
    pthread_mutex_destroy(&ai->mutex);

    if (ai->formulas) formula_collection_destroy(ai->formulas);
    if (ai->blockchain) kovian_chain_destroy(ai->blockchain);
    if (ai->memory) kolibri_memory_destroy(ai->memory);
    sensor_state_destroy(&ai->sensor_state);
    goal_set_destroy(&ai->goals);
    if (ai->pipeline) formula_training_pipeline_destroy(ai->pipeline);
    if (ai->policy_mlp) mlp_model_destroy(ai->policy_mlp);
    if (ai->world_model) transformer_model_destroy(ai->world_model);

    free(ai);
}

// Запуск AI-подсистемы
void kolibri_ai_start(KolibriAI* ai) {
    if (!ai || ai->running) return;
    
    ai->running = 1;
    pthread_create(&ai->ai_thread, NULL, ai_thread_function, ai);
}

// Остановка AI-подсистемы
void kolibri_ai_stop(KolibriAI* ai) {
    if (!ai || !ai->running) return;
    
    ai->running = 0;
    pthread_join(ai->ai_thread, NULL);
}

// Обработка одной итерации
#include "kolibri_log.h"

void kolibri_ai_process_iteration(KolibriAI* ai) {
    if (!ai) {
        return;
    }

    pthread_mutex_lock(&ai->mutex);

    kolibri_ai_collect_observations(ai);
    kolibri_ai_update_memory_from_sensors(ai);
    KolibriPlanningResult planning = kolibri_ai_plan_actions(ai);
    kolibri_ai_apply_reinforcement(ai, &planning);

    ai->iterations++;

    if (planning.best_hypothesis && planning.best_hypothesis->formula.effectiveness > 0.55 &&
        ai->iterations % 100 == 0) {
        char* json = serialize_formula(&planning.best_hypothesis->formula);
        if (json) {
            LOG_AI("Sharing trained formula: %.2f reward, score=%.2f",
                   planning.best_hypothesis->experience.reward, planning.planning_score);
            // TODO: передать json соседним узлам через сетевой слой
            free(json);
        }
    }

    if (ai->iterations % 250 == 0) {
        adjust_chain_difficulty(ai->blockchain);
        LOG_INFO("[AI] status: iter=%lu complexity=%d formulas=%zu avg_reward=%.3f",
                 ai->iterations, ai->complexity_level,
                 ai->formulas ? ai->formulas->count : 0,
                 ai->pipeline ? ai->pipeline->metrics.average_reward : 0.0);
    }

    pthread_mutex_unlock(&ai->mutex);
}

// Добавление внешней формулы
int kolibri_ai_add_formula(KolibriAI* ai, const Formula* formula) {
    if (!ai || !formula) return -1;
    
    pthread_mutex_lock(&ai->mutex);
    int result = formula_collection_add(ai->formulas, formula);
    pthread_mutex_unlock(&ai->mutex);
    
    return result;
}

// Получение лучшей формулы
Formula* kolibri_ai_get_best_formula(KolibriAI* ai) {
    if (!ai || ai->formulas->count == 0) return NULL;
    
    pthread_mutex_lock(&ai->mutex);
    
    Formula* best = NULL;
    double max_effectiveness = 0.0;
    
    for (size_t i = 0; i < ai->formulas->count; i++) {
        if (ai->formulas->formulas[i].effectiveness > max_effectiveness) {
            best = &ai->formulas->formulas[i];
            max_effectiveness = best->effectiveness;
        }
    }
    
    Formula* result = NULL;
    if (best) {
        result = calloc(1, sizeof(Formula));
        if (result && formula_copy(result, best) != 0) {
            free(result);
            result = NULL;
        }
    }
    
    pthread_mutex_unlock(&ai->mutex);
    return result;
}

// Сериализация состояния
char* kolibri_ai_serialize_state(const KolibriAI* ai) {
    if (!ai) return NULL;
    
    struct json_object *jobj = json_object_new_object();
    
    // Добавляем параметры
    json_object_object_add(jobj, "complexity_level", 
                          json_object_new_int(ai->complexity_level));
    json_object_object_add(jobj, "learning_rate",
                          json_object_new_double(ai->learning_rate));
    json_object_object_add(jobj, "iterations",
                          json_object_new_int64(ai->iterations));
    
    // Добавляем статистику формул
    json_object_object_add(jobj, "formula_count",
                          json_object_new_int(ai->formulas->count));
    json_object_object_add(jobj, "blockchain_length",
                          json_object_new_int(ai->blockchain->length));
    
    const char* json_str = json_object_to_json_string(jobj);
    char* result = strdup(json_str);
    json_object_put(jobj);
    
    return result;
}

// Обработка формулы от соседнего узла
int kolibri_ai_process_remote_formula(KolibriAI* ai, const char* json) {
    if (!ai || !json) return -1;

    const char* payload_str = json;
    struct json_object* root = json_tokener_parse(json);
    if (root && json_object_is_type(root, json_type_object)) {
        struct json_object* type_obj = NULL;
        if (json_object_object_get_ex(root, "type", &type_obj)) {
            const char* type = json_object_get_string(type_obj);
            if (type && strcmp(type, "formula") != 0) {
                json_object_put(root);
                return -1;
            }
        }

        struct json_object* payload = NULL;
        if (json_object_object_get_ex(root, "payload", &payload)) {
            if (json_object_is_type(payload, json_type_string)) {
                payload_str = json_object_get_string(payload);
            } else {
                payload_str = json_object_to_json_string(payload);
            }
        }
    }

    Formula* formula = deserialize_formula(payload_str);
    if (!formula) return -1;
    
    int result = -1;
    
    // Проверяем формулу
    if (formula->representation == FORMULA_REPRESENTATION_TEXT && validate_formula(formula)) {
        double dataset_score = 0.0;
        if (ai->pipeline && ai->pipeline->dataset.count > 0) {
            for (size_t i = 0; i < ai->pipeline->dataset.count; ++i) {
                const FormulaDatasetEntry* entry = &ai->pipeline->dataset.entries[i];
                dataset_score += fabs(entry->effectiveness) *
                                 kolibri_ai_text_overlap(formula->content, entry->task);
            }
            dataset_score /= (double)ai->pipeline->dataset.count;
        }

        double alignment = kolibri_ai_memory_alignment(ai, formula);
        formula->confirmations++;
        double confirmation_boost = 1.0 + log(formula->confirmations) / 10.0;

        double adjusted_effectiveness = (dataset_score * 0.8 + alignment * 0.2) * confirmation_boost;
        formula->effectiveness = adjusted_effectiveness;

        if (adjusted_effectiveness >= 0.35) {
            result = kolibri_ai_add_formula(ai, formula);
            if (adjusted_effectiveness >= 0.7) {
                LOG_AI("Broadcasting high-quality remote formula: %.4f",
                       adjusted_effectiveness);
            }
        }
    }
    
    formula_clear(formula);
    free(formula);
    if (root) {
        json_object_put(root);
    }

    if (result == 0) {
        printf("[NETWORK] Remote formula applied successfully\n");
    }

    return result;
}

// Синхронизация с соседним узлом
void kolibri_ai_sync_with_neighbor(KolibriAI* ai, const char* neighbor_url) {
    if (!ai || !neighbor_url) return;
    
    CURL *curl = curl_easy_init();
    if (!curl) return;
    
    // Получаем лучшую формулу
    Formula* best = kolibri_ai_get_best_formula(ai);
    if (!best) {
        curl_easy_cleanup(curl);
        return;
    }
    
    // Сериализуем формулу
    char* json = serialize_formula(best);
    if (!json) {
        formula_clear(best);
        free(best);
        curl_easy_cleanup(curl);
        return;
    }
    
    // Отправляем формулу соседу
    curl_easy_setopt(curl, CURLOPT_URL, neighbor_url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    
    curl_easy_perform(curl);
    
    free(json);
    formula_clear(best);
    free(best);
    curl_easy_cleanup(curl);
}
