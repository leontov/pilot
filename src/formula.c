#include "formula.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uuid/uuid.h>
#include <json-c/json.h>
#include <stdint.h>

static void formula_collection_reset_top(FormulaCollection* collection) {
    if (!collection) {
        return;
    }

    collection->best_indices[0] = SIZE_MAX;
    collection->best_indices[1] = SIZE_MAX;
    collection->best_count = 0;
}

static void formula_collection_consider_index(FormulaCollection* collection, size_t index) {
    if (!collection || index >= collection->count) {
        return;
    }

    const Formula* candidate = &collection->formulas[index];

    if (collection->best_count == 0) {
        collection->best_indices[0] = index;
        collection->best_count = 1;
        return;
    }

    size_t current_best = collection->best_indices[0];
    const Formula* best_formula = &collection->formulas[current_best];

    if (candidate->effectiveness > best_formula->effectiveness) {
        size_t previous_best = collection->best_indices[0];
        collection->best_indices[0] = index;
        if (collection->best_count == 1) {
            collection->best_indices[1] = previous_best;
            collection->best_count = 2;
        } else {
            collection->best_indices[1] = previous_best;
        }
        return;
    }

    if (collection->best_count == 1) {
        collection->best_indices[1] = index;
        collection->best_count = 2;
        return;
    }

    size_t current_second = collection->best_indices[1];
    const Formula* second_formula = &collection->formulas[current_second];

    if (candidate->effectiveness > second_formula->effectiveness) {
        collection->best_indices[1] = index;
    }
}

static void formula_collection_recompute_top(FormulaCollection* collection) {
    if (!collection) {
        return;
    }

    formula_collection_reset_top(collection);
    for (size_t i = 0; i < collection->count; i++) {
        formula_collection_consider_index(collection, i);
    }
}

// Определения типов формул
const int FORMULA_TYPE_SIMPLE = 0;
const int FORMULA_TYPE_POLYNOMIAL = 1;
const int FORMULA_TYPE_COMPOSITE = 2;
const int FORMULA_TYPE_PERIODIC = 3;

void formula_clear(Formula* formula) {
    if (!formula) {
        return;
    }

    free(formula->coefficients);
    formula->coefficients = NULL;
    formula->coeff_count = 0;

    free(formula->expression);
    formula->expression = NULL;
}

int formula_copy(Formula* dest, const Formula* src) {
    if (!dest || !src) {
        return -1;
    }

    formula_clear(dest);
    memset(dest, 0, sizeof(*dest));

    memcpy(dest->id, src->id, sizeof(dest->id));
    dest->effectiveness = src->effectiveness;
    dest->created_at = src->created_at;
    dest->tests_passed = src->tests_passed;
    dest->confirmations = src->confirmations;
    dest->representation = src->representation;
    dest->type = src->type;

    if (src->representation == FORMULA_REPRESENTATION_TEXT) {
        strncpy(dest->content, src->content, sizeof(dest->content) - 1);
        dest->content[sizeof(dest->content) - 1] = '\0';
    } else if (src->representation == FORMULA_REPRESENTATION_ANALYTIC) {
        dest->coeff_count = src->coeff_count;
        if (src->coeff_count > 0 && src->coefficients) {
            dest->coefficients = malloc(sizeof(double) * src->coeff_count);
            if (!dest->coefficients) {
                formula_clear(dest);
                return -1;
            }
            memcpy(dest->coefficients, src->coefficients, sizeof(double) * src->coeff_count);
        }

        if (src->expression) {
            dest->expression = strdup(src->expression);
            if (!dest->expression) {
                formula_clear(dest);
                return -1;
            }
        }
    }

    return 0;
}

// Создание коллекции формул
FormulaCollection* formula_collection_create(size_t initial_capacity) {
    FormulaCollection* collection = malloc(sizeof(FormulaCollection));
    if (!collection) return NULL;

    collection->formulas = calloc(initial_capacity, sizeof(Formula));
    if (!collection->formulas) {
        free(collection);
        return NULL;
    }

    collection->count = 0;
    collection->capacity = initial_capacity;
    formula_collection_reset_top(collection);
    return collection;
}

// Уничтожение коллекции
void formula_collection_destroy(FormulaCollection* collection) {
    if (collection) {
        for (size_t i = 0; i < collection->count; i++) {
            formula_clear(&collection->formulas[i]);
        }
        free(collection->formulas);
        free(collection);
    }
}

// Добавление формулы в коллекцию
int formula_collection_add(FormulaCollection* collection, const Formula* formula) {
    if (!collection || !formula) return -1;

    if (collection->count >= collection->capacity) {
        size_t new_capacity = collection->capacity * 2;
        Formula* new_formulas = realloc(collection->formulas, sizeof(Formula) * new_capacity);
        if (!new_formulas) return -1;

        // Zero initialise the new capacity to avoid stale pointers.
        memset(new_formulas + collection->capacity, 0,
               sizeof(Formula) * (new_capacity - collection->capacity));

        collection->formulas = new_formulas;
        collection->capacity = new_capacity;
    }

    Formula* dest = &collection->formulas[collection->count];
    if (formula_copy(dest, formula) != 0) {
        memset(dest, 0, sizeof(*dest));
        return -1;
    }

    collection->count++;
    formula_collection_consider_index(collection, collection->count - 1);
    return 0;
}

// Поиск формулы по ID
Formula* formula_collection_find(FormulaCollection* collection, const char* id) {
    if (!collection || !id) return NULL;

    for (size_t i = 0; i < collection->count; i++) {
        if (strcmp(collection->formulas[i].id, id) == 0) {
            return &collection->formulas[i];
        }
    }
    return NULL;
}

void formula_collection_remove(FormulaCollection* collection, const char* id) {
    if (!collection || !id) return;

    for (size_t i = 0; i < collection->count; i++) {
        if (strcmp(collection->formulas[i].id, id) == 0) {
            formula_clear(&collection->formulas[i]);
            if (i + 1 < collection->count) {
                memmove(&collection->formulas[i],
                        &collection->formulas[i + 1],
                        (collection->count - i - 1) * sizeof(Formula));
            }
            if (collection->count > 0) {
                memset(&collection->formulas[collection->count - 1], 0, sizeof(Formula));
                collection->count--;
                memset(&collection->formulas[collection->count], 0, sizeof(Formula));
            }
            formula_collection_recompute_top(collection);
            return;
        }
    }
}

size_t formula_collection_get_top(const FormulaCollection* collection,
                                  const Formula** out_formulas,
                                  size_t max_results) {
    if (!collection || !out_formulas || max_results == 0) {
        return 0;
    }

    size_t available = collection->best_count;
    if (available > max_results) {
        available = max_results;
    }

    size_t produced = 0;
    for (size_t i = 0; i < available; i++) {
        size_t index = collection->best_indices[i];
        if (index >= collection->count) {
            break;
        }
        out_formulas[produced++] = &collection->formulas[index];
    }

    return produced;
}

// Распознавание типа формулы
int get_formula_type(const char* content) {
    if (!content) return FORMULA_TYPE_SIMPLE;
    
    if (strstr(content, "sin") || strstr(content, "cos"))
        return FORMULA_TYPE_PERIODIC;
    else if (strstr(content, "^"))
        return FORMULA_TYPE_POLYNOMIAL;
    else if (strstr(content, "+") || strstr(content, "*"))
        return FORMULA_TYPE_COMPOSITE;
        
    return FORMULA_TYPE_SIMPLE;
}

// Валидация формулы
int validate_formula(const Formula* formula) {
    if (!formula) return 0;

    if (formula->representation != FORMULA_REPRESENTATION_TEXT) {
        return 0;
    }

    if (strlen(formula->content) == 0) return 0;
    if (formula->effectiveness < 0.0 || formula->effectiveness > 1.0) return 0;
    return 1;
}

// ----------- Новая обучающая подсистема формул -----------

static void formula_hypothesis_clear(FormulaHypothesis* hypothesis) {
    if (!hypothesis) {
        return;
    }

    formula_clear(&hypothesis->formula);
    memset(&hypothesis->experience, 0, sizeof(hypothesis->experience));
}

FormulaMemorySnapshot formula_memory_snapshot_clone(const FormulaMemoryFact* facts,
                                                   size_t count) {
    FormulaMemorySnapshot snapshot = {0};
    if (!facts || count == 0) {
        return snapshot;
    }

    snapshot.facts = calloc(count, sizeof(FormulaMemoryFact));
    if (!snapshot.facts) {
        return snapshot;
    }

    memcpy(snapshot.facts, facts, sizeof(FormulaMemoryFact) * count);
    snapshot.count = count;
    return snapshot;
}

void formula_memory_snapshot_release(FormulaMemorySnapshot* snapshot) {
    if (!snapshot) {
        return;
    }

    free(snapshot->facts);
    snapshot->facts = NULL;
    snapshot->count = 0;
}

static void formula_dataset_clear(FormulaDataset* dataset) {
    if (!dataset) {
        return;
    }
    free(dataset->entries);
    dataset->entries = NULL;
    dataset->count = 0;
}

static const size_t k_default_feature_dim = 8;
static const size_t k_default_hidden_dim = 12;
static const size_t k_default_transformer_dim = 8;

typedef struct {
    char magic[4];
    uint32_t version;
    uint32_t mlp_input_dim;
    uint32_t mlp_hidden_dim;
    uint32_t mlp_output_dim;
    uint32_t transformer_input_dim;
    uint32_t transformer_model_dim;
} FormulaWeightsHeader;

static void formula_mlp_model_release(FormulaMLPModel* model) {
    if (!model) {
        return;
    }
    free(model->input_weights);
    free(model->hidden_bias);
    free(model->output_weights);
    free(model->output_bias);
    memset(model, 0, sizeof(*model));
}

static int formula_mlp_model_configure(FormulaMLPModel* model,
                                       size_t input_dim,
                                       size_t hidden_dim,
                                       size_t output_dim) {
    if (!model || input_dim == 0 || hidden_dim == 0 || output_dim == 0) {
        return -1;
    }

    if (model->input_dim == input_dim && model->hidden_dim == hidden_dim &&
        model->output_dim == output_dim && model->input_weights &&
        model->hidden_bias && model->output_weights && model->output_bias) {
        return 0;
    }

    formula_mlp_model_release(model);

    model->input_dim = input_dim;
    model->hidden_dim = hidden_dim;
    model->output_dim = output_dim;

    size_t input_weights = input_dim * hidden_dim;
    size_t output_weights = hidden_dim * output_dim;

    model->input_weights = calloc(input_weights, sizeof(double));
    model->hidden_bias = calloc(hidden_dim, sizeof(double));
    model->output_weights = calloc(output_weights, sizeof(double));
    model->output_bias = calloc(output_dim, sizeof(double));

    if (!model->input_weights || !model->hidden_bias || !model->output_weights ||
        !model->output_bias) {
        formula_mlp_model_release(model);
        return -1;
    }

    return 0;
}

static void formula_transformer_model_release(FormulaTransformerModel* model) {
    if (!model) {
        return;
    }
    free(model->w_q);
    free(model->w_k);
    free(model->w_v);
    free(model->w_o);
    memset(model, 0, sizeof(*model));
}

static int formula_transformer_model_configure(FormulaTransformerModel* model,
                                               size_t input_dim,
                                               size_t model_dim) {
    if (!model || input_dim == 0 || model_dim == 0) {
        return -1;
    }

    if (model->input_dim == input_dim && model->model_dim == model_dim &&
        model->w_q && model->w_k && model->w_v && model->w_o) {
        return 0;
    }

    formula_transformer_model_release(model);

    model->input_dim = input_dim;
    model->model_dim = model_dim;
    model->bias = 0.0;

    size_t matrix_size = model_dim * input_dim;

    model->w_q = calloc(matrix_size, sizeof(double));
    model->w_k = calloc(matrix_size, sizeof(double));
    model->w_v = calloc(matrix_size, sizeof(double));
    model->w_o = calloc(model_dim, sizeof(double));

    if (!model->w_q || !model->w_k || !model->w_v || !model->w_o) {
        formula_transformer_model_release(model);
        return -1;
    }

    return 0;
}

static double random_weight(double scale) {
    if (scale <= 0.0) {
        scale = 1.0;
    }
    double unit = (double)rand() / (double)RAND_MAX;
    return (unit * 2.0 - 1.0) * scale;
}

static void fill_random(double* data, size_t count, double scale) {
    if (!data) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        data[i] = random_weight(scale);
    }
}

static void formula_training_pipeline_randomize_models(FormulaTrainingPipeline* pipeline) {
    if (!pipeline) {
        return;
    }

    FormulaMLPModel* mlp = &pipeline->mlp_model;
    FormulaTransformerModel* transformer = &pipeline->transformer_model;

    if (!mlp->input_weights || !mlp->hidden_bias || !mlp->output_weights || !mlp->output_bias) {
        return;
    }

    size_t mlp_input_weights = mlp->input_dim * mlp->hidden_dim;
    size_t mlp_output_weights = mlp->hidden_dim * mlp->output_dim;

    fill_random(mlp->input_weights, mlp_input_weights, 0.15);
    fill_random(mlp->hidden_bias, mlp->hidden_dim, 0.05);
    fill_random(mlp->output_weights, mlp_output_weights, 0.15);
    fill_random(mlp->output_bias, mlp->output_dim, 0.05);

    if (!transformer->w_q || !transformer->w_k || !transformer->w_v || !transformer->w_o) {
        return;
    }

    size_t transformer_matrix = transformer->model_dim * transformer->input_dim;
    fill_random(transformer->w_q, transformer_matrix, 0.1);
    fill_random(transformer->w_k, transformer_matrix, 0.1);
    fill_random(transformer->w_v, transformer_matrix, 0.1);
    fill_random(transformer->w_o, transformer->model_dim, 0.1);
    transformer->bias = random_weight(0.05);
}

static int read_double_array(const unsigned char** cursor,
                             size_t* remaining,
                             double* destination,
                             size_t count) {
    if (!cursor || !remaining || !destination) {
        return -1;
    }

    size_t bytes_needed = count * sizeof(double);
    if (*remaining < bytes_needed) {
        return -1;
    }

    memcpy(destination, *cursor, bytes_needed);
    *cursor += bytes_needed;
    *remaining -= bytes_needed;
    return 0;
}

static int formula_training_pipeline_apply_weights_buffer(FormulaTrainingPipeline* pipeline,
                                                          const unsigned char* buffer,
                                                          size_t size) {
    if (!pipeline || !buffer || size < sizeof(FormulaWeightsHeader)) {
        return -1;
    }

    FormulaWeightsHeader header;
    memcpy(&header, buffer, sizeof(header));
    if (memcmp(header.magic, "KAIW", sizeof(header.magic)) != 0 || header.version != 1) {
        return -1;
    }

    size_t mlp_input_dim = header.mlp_input_dim;
    size_t mlp_hidden_dim = header.mlp_hidden_dim;
    size_t mlp_output_dim = header.mlp_output_dim;
    size_t transformer_input_dim = header.transformer_input_dim;
    size_t transformer_model_dim = header.transformer_model_dim;

    if (mlp_input_dim == 0 || mlp_hidden_dim == 0 || mlp_output_dim == 0 ||
        transformer_input_dim == 0 || transformer_model_dim == 0) {
        return -1;
    }

    if (formula_mlp_model_configure(&pipeline->mlp_model,
                                    mlp_input_dim,
                                    mlp_hidden_dim,
                                    mlp_output_dim) != 0) {
        return -1;
    }

    if (formula_transformer_model_configure(&pipeline->transformer_model,
                                            transformer_input_dim,
                                            transformer_model_dim) != 0) {
        return -1;
    }

    const unsigned char* cursor = buffer + sizeof(header);
    size_t remaining = size - sizeof(header);

    size_t mlp_input_weights = mlp_input_dim * mlp_hidden_dim;
    size_t mlp_output_weights = mlp_hidden_dim * mlp_output_dim;
    size_t transformer_matrix = transformer_model_dim * transformer_input_dim;

    if (read_double_array(&cursor, &remaining, pipeline->mlp_model.input_weights, mlp_input_weights) != 0 ||
        read_double_array(&cursor, &remaining, pipeline->mlp_model.hidden_bias, mlp_hidden_dim) != 0 ||
        read_double_array(&cursor, &remaining, pipeline->mlp_model.output_weights, mlp_output_weights) != 0 ||
        read_double_array(&cursor, &remaining, pipeline->mlp_model.output_bias, mlp_output_dim) != 0 ||
        read_double_array(&cursor, &remaining, pipeline->transformer_model.w_q, transformer_matrix) != 0 ||
        read_double_array(&cursor, &remaining, pipeline->transformer_model.w_k, transformer_matrix) != 0 ||
        read_double_array(&cursor, &remaining, pipeline->transformer_model.w_v, transformer_matrix) != 0 ||
        read_double_array(&cursor, &remaining, pipeline->transformer_model.w_o, transformer_model_dim) != 0) {
        return -1;
    }

    if (remaining < sizeof(double)) {
        return -1;
    }

    double bias = 0.0;
    memcpy(&bias, cursor, sizeof(double));
    pipeline->transformer_model.bias = bias;

    return 0;
}

static void write_double_array(unsigned char** cursor,
                               const double* source,
                               size_t count) {
    if (!cursor || !*cursor || !source) {
        return;
    }
    size_t bytes = count * sizeof(double);
    memcpy(*cursor, source, bytes);
    *cursor += bytes;
}

int formula_training_pipeline_sync_weights_buffer(FormulaTrainingPipeline* pipeline) {
    if (!pipeline) {
        return -1;
    }

    FormulaMLPModel* mlp = &pipeline->mlp_model;
    FormulaTransformerModel* transformer = &pipeline->transformer_model;

    if (!mlp->input_weights || !mlp->hidden_bias || !mlp->output_weights || !mlp->output_bias ||
        !transformer->w_q || !transformer->w_k || !transformer->w_v || !transformer->w_o) {
        return -1;
    }

    size_t mlp_input_weights = mlp->input_dim * mlp->hidden_dim;
    size_t mlp_output_weights = mlp->hidden_dim * mlp->output_dim;
    size_t transformer_matrix = transformer->model_dim * transformer->input_dim;

    size_t total_doubles = mlp_input_weights + mlp->hidden_dim + mlp_output_weights + mlp->output_dim +
                           transformer_matrix * 3 + transformer->model_dim + 1;
    size_t total_bytes = sizeof(FormulaWeightsHeader) + total_doubles * sizeof(double);

    unsigned char* buffer = malloc(total_bytes);
    if (!buffer) {
        return -1;
    }

    FormulaWeightsHeader header = {{'K', 'A', 'I', 'W'},
                                   1,
                                   (uint32_t)mlp->input_dim,
                                   (uint32_t)mlp->hidden_dim,
                                   (uint32_t)mlp->output_dim,
                                   (uint32_t)transformer->input_dim,
                                   (uint32_t)transformer->model_dim};
    memcpy(buffer, &header, sizeof(header));

    unsigned char* cursor = buffer + sizeof(header);
    write_double_array(&cursor, mlp->input_weights, mlp_input_weights);
    write_double_array(&cursor, mlp->hidden_bias, mlp->hidden_dim);
    write_double_array(&cursor, mlp->output_weights, mlp_output_weights);
    write_double_array(&cursor, mlp->output_bias, mlp->output_dim);
    write_double_array(&cursor, transformer->w_q, transformer_matrix);
    write_double_array(&cursor, transformer->w_k, transformer_matrix);
    write_double_array(&cursor, transformer->w_v, transformer_matrix);
    write_double_array(&cursor, transformer->w_o, transformer->model_dim);
    memcpy(cursor, &transformer->bias, sizeof(double));

    free(pipeline->weights);
    pipeline->weights = buffer;
    pipeline->weights_size = total_bytes;
    return 0;
}

static void formula_training_metrics_reset(FormulaTrainingMetrics* metrics) {
    if (!metrics) {
        return;
    }
    memset(metrics, 0, sizeof(*metrics));
}

FormulaTrainingPipeline* formula_training_pipeline_create(size_t capacity) {
    FormulaTrainingPipeline* pipeline = calloc(1, sizeof(FormulaTrainingPipeline));
    if (!pipeline) {
        return NULL;
    }

    pipeline->candidates.hypotheses = calloc(capacity, sizeof(FormulaHypothesis));
    if (!pipeline->candidates.hypotheses) {
        free(pipeline);
        return NULL;
    }

    pipeline->candidates.capacity = capacity;
    pipeline->candidates.count = 0;
    formula_training_metrics_reset(&pipeline->metrics);

    if (formula_mlp_model_configure(&pipeline->mlp_model,
                                    k_default_feature_dim,
                                    k_default_hidden_dim,
                                    1) != 0) {
        free(pipeline->candidates.hypotheses);
        free(pipeline);
        return NULL;
    }

    if (formula_transformer_model_configure(&pipeline->transformer_model,
                                            k_default_feature_dim,
                                            k_default_transformer_dim) != 0) {
        formula_mlp_model_release(&pipeline->mlp_model);
        free(pipeline->candidates.hypotheses);
        free(pipeline);
        return NULL;
    }

    formula_training_pipeline_randomize_models(pipeline);
    if (formula_training_pipeline_sync_weights_buffer(pipeline) != 0) {
        formula_transformer_model_release(&pipeline->transformer_model);
        formula_mlp_model_release(&pipeline->mlp_model);
        free(pipeline->candidates.hypotheses);
        free(pipeline);
        return NULL;
    }

    return pipeline;
}

void formula_training_pipeline_destroy(FormulaTrainingPipeline* pipeline) {
    if (!pipeline) {
        return;
    }

    for (size_t i = 0; i < pipeline->candidates.capacity; ++i) {
        formula_hypothesis_clear(&pipeline->candidates.hypotheses[i]);
    }
    free(pipeline->candidates.hypotheses);
    pipeline->candidates.hypotheses = NULL;
    pipeline->candidates.capacity = 0;
    pipeline->candidates.count = 0;

    formula_dataset_clear(&pipeline->dataset);
    formula_memory_snapshot_release(&pipeline->memory_snapshot);

    formula_transformer_model_release(&pipeline->transformer_model);
    formula_mlp_model_release(&pipeline->mlp_model);

    free(pipeline->weights);
    pipeline->weights = NULL;
    pipeline->weights_size = 0;

    free(pipeline);
}

static int read_file_bytes(const char* path, unsigned char** buffer, size_t* size) {
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

int formula_training_pipeline_load_dataset(FormulaTrainingPipeline* pipeline,
                                          const char* path) {
    if (!pipeline || !path) {
        return -1;
    }

    formula_dataset_clear(&pipeline->dataset);

    struct json_object* root = json_object_from_file(path);
    if (!root) {
        return -1;
    }

    if (!json_object_is_type(root, json_type_array)) {
        json_object_put(root);
        return -1;
    }

    size_t count = (size_t)json_object_array_length(root);
    if (count == 0) {
        json_object_put(root);
        return 0;
    }

    pipeline->dataset.entries = calloc(count, sizeof(FormulaDatasetEntry));
    if (!pipeline->dataset.entries) {
        json_object_put(root);
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        struct json_object* entry = json_object_array_get_idx(root, (int)i);
        if (!entry) {
            continue;
        }

        FormulaDatasetEntry* target = &pipeline->dataset.entries[i];
        struct json_object* value = NULL;

        if (json_object_object_get_ex(entry, "task", &value)) {
            const char* task = json_object_get_string(value);
            if (task) {
                strncpy(target->task, task, sizeof(target->task) - 1);
            }
        }

        if (json_object_object_get_ex(entry, "response", &value)) {
            const char* response = json_object_get_string(value);
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
    }

    pipeline->dataset.count = count;
    json_object_put(root);
    return 0;
}

int formula_training_pipeline_load_weights(FormulaTrainingPipeline* pipeline,
                                          const char* path) {
    if (!pipeline || !path) {
        return -1;
    }

    unsigned char* buffer = NULL;
    size_t size = 0;
    if (read_file_bytes(path, &buffer, &size) != 0) {
        formula_training_pipeline_randomize_models(pipeline);
        formula_training_pipeline_sync_weights_buffer(pipeline);
        return -1;
    }

    int rc = formula_training_pipeline_apply_weights_buffer(pipeline, buffer, size);
    free(buffer);
    if (rc != 0) {
        formula_training_pipeline_randomize_models(pipeline);
        formula_training_pipeline_sync_weights_buffer(pipeline);
        return -1;
    }

    return formula_training_pipeline_sync_weights_buffer(pipeline);
}

static void formula_training_pipeline_reset_candidates(FormulaTrainingPipeline* pipeline) {
    if (!pipeline) {
        return;
    }

    for (size_t i = 0; i < pipeline->candidates.count; ++i) {
        formula_hypothesis_clear(&pipeline->candidates.hypotheses[i]);
    }
    pipeline->candidates.count = 0;
}

static void formula_training_pipeline_add_candidate(FormulaTrainingPipeline* pipeline,
                                                    const Formula* formula,
                                                    const char* source) {
    if (!pipeline || !formula || pipeline->candidates.count >= pipeline->candidates.capacity) {
        return;
    }

    FormulaHypothesis* target = &pipeline->candidates.hypotheses[pipeline->candidates.count++];
    formula_hypothesis_clear(target);
    formula_copy(&target->formula, formula);
    strncpy(target->experience.source, source ? source : "unknown",
            sizeof(target->experience.source) - 1);
    target->experience.source[sizeof(target->experience.source) - 1] = '\0';
}

static void formula_training_pipeline_add_from_memory(FormulaTrainingPipeline* pipeline,
                                                      const FormulaMemorySnapshot* snapshot,
                                                      size_t max_candidates) {
    if (!pipeline || !snapshot) {
        return;
    }

    size_t limit = snapshot->count < max_candidates ? snapshot->count : max_candidates;
    for (size_t i = 0; i < limit && pipeline->candidates.count < pipeline->candidates.capacity; ++i) {
        const FormulaMemoryFact* fact = &snapshot->facts[i];
        Formula synthetic = {0};
        uuid_t uuid;
        uuid_generate(uuid);
        uuid_unparse(uuid, synthetic.id);
        synthetic.representation = FORMULA_REPRESENTATION_TEXT;
        snprintf(synthetic.content, sizeof(synthetic.content),
                 "f(x) = context('%s') * %.2f",
                 fact->description, fmax(0.1, fact->importance));
        synthetic.created_at = fact->timestamp;
        synthetic.effectiveness = fmax(0.0, fact->reward);
        formula_training_pipeline_add_candidate(pipeline, &synthetic, "memory");
        formula_clear(&synthetic);
    }
}

int formula_training_pipeline_prepare(FormulaTrainingPipeline* pipeline,
                                      const FormulaCollection* library,
                                      const FormulaMemorySnapshot* snapshot,
                                      size_t max_candidates) {
    if (!pipeline) {
        return -1;
    }

    formula_training_pipeline_reset_candidates(pipeline);
    formula_memory_snapshot_release(&pipeline->memory_snapshot);
    if (snapshot && snapshot->count > 0) {
        pipeline->memory_snapshot = formula_memory_snapshot_clone(snapshot->facts, snapshot->count);
    }

    if (library && library->count > 0) {
        size_t limit = library->count < max_candidates ? library->count : max_candidates;
        for (size_t i = 0; i < limit; ++i) {
            const Formula* formula = &library->formulas[i];
            formula_training_pipeline_add_candidate(pipeline, formula, "library");
        }
    }

    size_t remaining = 0;
    if (max_candidates > pipeline->candidates.count) {
        remaining = max_candidates - pipeline->candidates.count;
    }
    if (remaining > 0) {
        formula_training_pipeline_add_from_memory(pipeline, &pipeline->memory_snapshot, remaining);
    }

    if (pipeline->candidates.count == 0 && pipeline->candidates.capacity > 0) {
        Formula bootstrap = {0};
        uuid_t uuid;
        uuid_generate(uuid);
        uuid_unparse(uuid, bootstrap.id);
        bootstrap.representation = FORMULA_REPRESENTATION_TEXT;
        strncpy(bootstrap.content, "f(x) = x", sizeof(bootstrap.content) - 1);
        bootstrap.created_at = time(NULL);
        formula_training_pipeline_add_candidate(pipeline, &bootstrap, "bootstrap");
        formula_clear(&bootstrap);
    }

    return 0;
}

static double compute_text_overlap(const char* a, const char* b) {
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

static double compute_memory_alignment(const FormulaHypothesis* hypothesis,
                                       const FormulaMemorySnapshot* snapshot) {
    if (!hypothesis || !snapshot || snapshot->count == 0) {
        return 0.0;
    }

    double total = 0.0;
    for (size_t i = 0; i < snapshot->count; ++i) {
        const FormulaMemoryFact* fact = &snapshot->facts[i];
        double overlap = compute_text_overlap(hypothesis->formula.content, fact->description);
        total += overlap * fmax(0.1, fact->importance);
    }

    return total / (double)snapshot->count;
}

int formula_training_pipeline_evaluate(FormulaTrainingPipeline* pipeline,
                                       FormulaCollection* library) {
    if (!pipeline) {
        return -1;
    }

    double preserved_mlp_loss = pipeline->metrics.last_mlp_loss;
    double preserved_transformer_loss = pipeline->metrics.last_transformer_loss;
    size_t preserved_training_steps = pipeline->metrics.total_training_steps;
    formula_training_metrics_reset(&pipeline->metrics);
    pipeline->metrics.last_mlp_loss = preserved_mlp_loss;
    pipeline->metrics.last_transformer_loss = preserved_transformer_loss;
    pipeline->metrics.total_training_steps = preserved_training_steps;
    if (pipeline->candidates.count == 0) {
        return 0;
    }

    double total_reward = 0.0;
    double total_imitation = 0.0;
    double total_success = 0.0;

    for (size_t i = 0; i < pipeline->candidates.count; ++i) {
        FormulaHypothesis* hypothesis = &pipeline->candidates.hypotheses[i];
        double best_alignment = 0.0;
        double accumulated_effectiveness = 0.0;
        const FormulaDatasetEntry* best_entry = NULL;

        for (size_t j = 0; j < pipeline->dataset.count; ++j) {
            const FormulaDatasetEntry* entry = &pipeline->dataset.entries[j];
            double overlap = compute_text_overlap(hypothesis->formula.content, entry->task);
            double alignment = overlap * fabs(entry->effectiveness);
            accumulated_effectiveness += alignment;
            if (alignment > best_alignment) {
                best_alignment = alignment;
                best_entry = entry;
            }
        }

        double reward = 0.0;
        if (pipeline->dataset.count > 0) {
            reward = accumulated_effectiveness / (double)pipeline->dataset.count;
        }

        double imitation = compute_memory_alignment(hypothesis, &pipeline->memory_snapshot);
        double success = reward > 0.2 ? 1.0 : reward;

        hypothesis->experience.reward = reward;
        hypothesis->experience.imitation_score = imitation;
        hypothesis->experience.accuracy = fmax(0.0, best_alignment);
        hypothesis->experience.loss = fmax(0.0, 1.0 - reward);
        if (best_entry) {
            strncpy(hypothesis->experience.task_id, best_entry->task,
                    sizeof(hypothesis->experience.task_id) - 1);
        }

        hypothesis->formula.effectiveness = reward;

        if (library && reward > 0.35) {
            formula_collection_add(library, &hypothesis->formula);
        }

        total_reward += reward;
        total_imitation += imitation;
        total_success += success;
    }

    pipeline->metrics.total_evaluated = pipeline->candidates.count;
    pipeline->metrics.average_reward = total_reward / (double)pipeline->candidates.count;
    pipeline->metrics.average_imitation = total_imitation / (double)pipeline->candidates.count;
    pipeline->metrics.success_rate = total_success / (double)pipeline->candidates.count;
    return 0;
}

FormulaHypothesis* formula_training_pipeline_select_best(FormulaTrainingPipeline* pipeline) {
    if (!pipeline || pipeline->candidates.count == 0) {
        return NULL;
    }

    size_t best_index = 0;
    double best_score = -1.0;
    for (size_t i = 0; i < pipeline->candidates.count; ++i) {
        FormulaHypothesis* hypothesis = &pipeline->candidates.hypotheses[i];
        double score = hypothesis->experience.reward + 0.2 * hypothesis->experience.imitation_score;
        if (score > best_score) {
            best_index = i;
            best_score = score;
        }
    }

    return &pipeline->candidates.hypotheses[best_index];
}

int formula_training_pipeline_record_experience(FormulaTrainingPipeline* pipeline,
                                               const FormulaExperience* experience) {
    if (!pipeline || !experience) {
        return -1;
    }

    pipeline->metrics.total_evaluated++;
    pipeline->metrics.average_reward =
        (pipeline->metrics.average_reward * (pipeline->metrics.total_evaluated - 1) +
         experience->reward) /
        (double)pipeline->metrics.total_evaluated;
    pipeline->metrics.average_imitation =
        (pipeline->metrics.average_imitation * (pipeline->metrics.total_evaluated - 1) +
         experience->imitation_score) /
        (double)pipeline->metrics.total_evaluated;
    pipeline->metrics.success_rate =
        (pipeline->metrics.success_rate * (pipeline->metrics.total_evaluated - 1) +
         (experience->reward > 0.2 ? 1.0 : experience->reward)) /
        (double)pipeline->metrics.total_evaluated;
    return 0;
}

// Сериализация формулы в JSON
char* serialize_formula(const Formula* formula) {
    if (!formula) return NULL;

    struct json_object *jobj = json_object_new_object();
    json_object_object_add(jobj, "id", json_object_new_string(formula->id));
    json_object_object_add(jobj, "effectiveness", json_object_new_double(formula->effectiveness));
    json_object_object_add(jobj, "created_at", json_object_new_int64(formula->created_at));
    json_object_object_add(jobj, "tests_passed", json_object_new_int(formula->tests_passed));
    json_object_object_add(jobj, "confirmations", json_object_new_int(formula->confirmations));
    json_object_object_add(jobj, "representation", json_object_new_int(formula->representation));

    if (formula->representation == FORMULA_REPRESENTATION_TEXT) {
        json_object_object_add(jobj, "content", json_object_new_string(formula->content));
    } else if (formula->representation == FORMULA_REPRESENTATION_ANALYTIC) {
        json_object_object_add(jobj, "type", json_object_new_int(formula->type));
        struct json_object* coeffs = json_object_new_array();
        for (size_t i = 0; i < formula->coeff_count; i++) {
            json_object_array_add(coeffs, json_object_new_double(formula->coefficients[i]));
        }
        json_object_object_add(jobj, "coefficients", coeffs);

        if (formula->expression) {
            json_object_object_add(jobj, "expression", json_object_new_string(formula->expression));
        }
    }

    const char* json_str = json_object_to_json_string(jobj);
    char* result = strdup(json_str);
    json_object_put(jobj);

    return result;
}

// Десериализация формулы из JSON
Formula* deserialize_formula(const char* json_str) {
    if (!json_str) return NULL;
    
    struct json_object *jobj = json_tokener_parse(json_str);
    if (!jobj) return NULL;
    
    Formula* formula = calloc(1, sizeof(Formula));
    if (!formula) {
        json_object_put(jobj);
        return NULL;
    }

    struct json_object *id_obj, *effectiveness_obj, *created_at_obj,
                      *tests_passed_obj, *confirmations_obj, *representation_obj;

    if (!json_object_object_get_ex(jobj, "id", &id_obj) ||
        !json_object_object_get_ex(jobj, "effectiveness", &effectiveness_obj) ||
        !json_object_object_get_ex(jobj, "created_at", &created_at_obj) ||
        !json_object_object_get_ex(jobj, "tests_passed", &tests_passed_obj) ||
        !json_object_object_get_ex(jobj, "confirmations", &confirmations_obj) ||
        !json_object_object_get_ex(jobj, "representation", &representation_obj)) {

        free(formula);
        json_object_put(jobj);
        return NULL;
    }

    strncpy(formula->id, json_object_get_string(id_obj), sizeof(formula->id) - 1);
    formula->id[sizeof(formula->id) - 1] = '\0';
    formula->effectiveness = json_object_get_double(effectiveness_obj);
    formula->created_at = json_object_get_int64(created_at_obj);
    formula->tests_passed = json_object_get_int(tests_passed_obj);
    formula->confirmations = json_object_get_int(confirmations_obj);
    formula->representation = json_object_get_int(representation_obj);

    if (formula->representation == FORMULA_REPRESENTATION_TEXT) {
        struct json_object* content_obj;
        if (!json_object_object_get_ex(jobj, "content", &content_obj)) {
            free(formula);
            json_object_put(jobj);
            return NULL;
        }
        strncpy(formula->content, json_object_get_string(content_obj), sizeof(formula->content) - 1);
        formula->content[sizeof(formula->content) - 1] = '\0';
    } else if (formula->representation == FORMULA_REPRESENTATION_ANALYTIC) {
        struct json_object* type_obj;
        if (!json_object_object_get_ex(jobj, "type", &type_obj)) {
            free(formula);
            json_object_put(jobj);
            return NULL;
        }
        formula->type = json_object_get_int(type_obj);

        struct json_object* coeffs_obj;
        if (json_object_object_get_ex(jobj, "coefficients", &coeffs_obj) &&
            json_object_is_type(coeffs_obj, json_type_array)) {
            size_t coeff_count = json_object_array_length(coeffs_obj);
            if (coeff_count > 0) {
                formula->coefficients = malloc(sizeof(double) * coeff_count);
                if (!formula->coefficients) {
                    free(formula);
                    json_object_put(jobj);
                    return NULL;
                }
                formula->coeff_count = coeff_count;
                for (size_t i = 0; i < coeff_count; i++) {
                    struct json_object* value = json_object_array_get_idx(coeffs_obj, i);
                    formula->coefficients[i] = json_object_get_double(value);
                }
            }
        }

        struct json_object* expression_obj;
        if (json_object_object_get_ex(jobj, "expression", &expression_obj)) {
            formula->expression = strdup(json_object_get_string(expression_obj));
            if (!formula->expression) {
                formula_clear(formula);
                free(formula);
                json_object_put(jobj);
                return NULL;
            }
        }
    }

    json_object_put(jobj);
    return formula;
}

// Функция для динамического изменения сложности формул
static int calculate_dynamic_complexity(int base_complexity, int iteration) {
    return base_complexity + (iteration % 5); // Пример: сложность увеличивается каждые 5 итераций
}

// Добавление итерации для динамической сложности
void example_dynamic_complexity() {
    static int iteration = 0; // Локальная статическая переменная для отслеживания итераций
    iteration++;
    int dynamic_complexity = calculate_dynamic_complexity(FORMULA_TYPE_SIMPLE, iteration);
    // ...дальнейшая обработка dynamic_complexity...
    (void)dynamic_complexity;
}
