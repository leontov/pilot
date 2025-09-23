#include "formula.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uuid/uuid.h>
#include <json-c/json.h>

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
            }
            return;
        }
    }
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

    free(pipeline->weights);
    pipeline->weights = NULL;
    pipeline->weights_size = 0;

    return read_file_bytes(path, &pipeline->weights, &pipeline->weights_size);
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

    formula_training_metrics_reset(&pipeline->metrics);
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
