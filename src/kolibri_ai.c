#define _POSIX_C_SOURCE 200809L

#include "kolibri_ai.h"

#include "formula.h"

#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct KolibriAI {
    pthread_t worker;
    int running;
    pthread_mutex_t mutex;

    FormulaCollection *library;
    FormulaTrainingPipeline *pipeline;

    double average_reward;
    double exploration_rate;
    double exploitation_rate;
    uint64_t iterations;

    size_t training_interval;
    size_t transformer_interval;
    size_t training_batch_size;
    size_t transformer_batch_size;
    double mlp_learning_rate;
    double transformer_learning_rate;
    size_t training_cursor;
    size_t transformer_cursor;
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

static double clamp_double(double value, double min_value, double max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static double ratio_digits(const char *text) {
    if (!text || *text == '\0') {
        return 0.0;
    }
    size_t length = 0;
    size_t digits = 0;
    for (const unsigned char *ptr = (const unsigned char *)text; *ptr; ++ptr) {
        if (isdigit(*ptr)) {
            digits++;
        }
        length++;
    }
    if (length == 0) {
        return 0.0;
    }
    return (double)digits / (double)length;
}

static double ratio_letters(const char *text) {
    if (!text || *text == '\0') {
        return 0.0;
    }
    size_t length = 0;
    size_t letters = 0;
    for (const unsigned char *ptr = (const unsigned char *)text; *ptr; ++ptr) {
        if (isalpha(*ptr)) {
            letters++;
        }
        length++;
    }
    if (length == 0) {
        return 0.0;
    }
    return (double)letters / (double)length;
}

static double timestamp_feature(time_t timestamp) {
    if (timestamp == 0) {
        return 0.0;
    }
    double day = 24.0 * 3600.0;
    double normalized = fmod(fabs((double)timestamp), day);
    if (normalized < 0.0) {
        normalized += day;
    }
    return normalized / day;
}

static void dataset_entry_features(const FormulaDatasetEntry *entry,
                                   double *features,
                                   size_t feature_count) {
    if (!features || feature_count == 0) {
        return;
    }

    double base[8] = {0};
    size_t used = sizeof(base) / sizeof(base[0]);
    if (entry) {
        size_t task_len = strlen(entry->task);
        size_t response_len = strlen(entry->response);
        base[0] = clamp_double((double)task_len / 256.0, 0.0, 2.0);
        base[1] = clamp_double((double)response_len / 512.0, 0.0, 2.0);
        base[2] = clamp_double(entry->effectiveness, -1.0, 1.0);
        base[3] = clamp_double((double)entry->rating / 10.0, -1.0, 1.0);
        base[4] = ratio_digits(entry->task);
        base[5] = ratio_digits(entry->response) - ratio_letters(entry->response);
        base[6] = clamp_double(((double)response_len - (double)task_len) / 512.0, -1.0, 1.0);
        base[7] = timestamp_feature(entry->timestamp);
    }

    size_t limit = feature_count < used ? feature_count : used;
    for (size_t i = 0; i < limit; ++i) {
        features[i] = base[i];
    }
    for (size_t i = limit; i < feature_count; ++i) {
        features[i] = 0.0;
    }
}

static void kolibri_ai_seed_default_dataset(KolibriAI *ai) {
    if (!ai || !ai->pipeline || ai->pipeline->dataset.count > 0) {
        return;
    }

    size_t count = sizeof(k_default_formulas) / sizeof(k_default_formulas[0]);
    FormulaDatasetEntry *entries = calloc(count, sizeof(FormulaDatasetEntry));
    if (!entries) {
        return;
    }

    time_t now = time(NULL);
    for (size_t i = 0; i < count; ++i) {
        FormulaDatasetEntry *entry = &entries[i];
        const default_formula_t *formula = &k_default_formulas[i];
        snprintf(entry->task, sizeof(entry->task), "derive:%s", formula->id);
        snprintf(entry->response, sizeof(entry->response), "%s", formula->content);
        entry->effectiveness = formula->effectiveness;
        entry->rating = (int)lrint(clamp_double(formula->effectiveness * 10.0, 0.0, 10.0));
        entry->timestamp = now - (time_t)(i * 3600);
    }

    ai->pipeline->dataset.entries = entries;
    ai->pipeline->dataset.count = count;
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
    }

    kolibri_ai_seed_default_dataset(ai);
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

static double mlp_model_train_batch(FormulaTrainingPipeline *pipeline,
                                    size_t cursor,
                                    size_t batch_size,
                                    double learning_rate) {
    if (!pipeline || batch_size == 0) {
        return -1.0;
    }

    FormulaMLPModel *model = &pipeline->mlp_model;
    const FormulaDataset *dataset = &pipeline->dataset;
    if (!model->input_weights || dataset->count == 0) {
        return -1.0;
    }

    size_t dataset_count = dataset->count;
    size_t input_dim = model->input_dim;
    size_t hidden_dim = model->hidden_dim;
    size_t output_dim = model->output_dim;
    size_t effective_batch = batch_size > dataset_count ? dataset_count : batch_size;
    size_t start = dataset_count ? cursor % dataset_count : 0;

    double *features = calloc(input_dim, sizeof(double));
    double *hidden = calloc(hidden_dim, sizeof(double));
    double *hidden_grad = calloc(hidden_dim, sizeof(double));
    double *outputs = calloc(output_dim, sizeof(double));
    double *output_grad = calloc(output_dim, sizeof(double));
    if (!features || !hidden || !hidden_grad || !outputs || !output_grad) {
        free(features);
        free(hidden);
        free(hidden_grad);
        free(outputs);
        free(output_grad);
        return -1.0;
    }

    double total_loss = 0.0;
    for (size_t b = 0; b < effective_batch; ++b) {
        size_t index = (start + b) % dataset_count;
        const FormulaDatasetEntry *entry = &dataset->entries[index];
        dataset_entry_features(entry, features, input_dim);

        for (size_t h = 0; h < hidden_dim; ++h) {
            double sum = model->hidden_bias[h];
            size_t offset = h * input_dim;
            for (size_t i = 0; i < input_dim; ++i) {
                sum += model->input_weights[offset + i] * features[i];
            }
            hidden[h] = tanh(sum);
        }

        for (size_t o = 0; o < output_dim; ++o) {
            double sum = model->output_bias[o];
            size_t offset = o * hidden_dim;
            for (size_t h = 0; h < hidden_dim; ++h) {
                sum += model->output_weights[offset + h] * hidden[h];
            }
            outputs[o] = sum;
        }

        for (size_t o = 0; o < output_dim; ++o) {
            double target = (o == 0 && entry) ? entry->effectiveness : 0.0;
            double error = outputs[o] - target;
            total_loss += 0.5 * error * error;
            output_grad[o] = error;
        }

        for (size_t h = 0; h < hidden_dim; ++h) {
            double back = 0.0;
            for (size_t o = 0; o < output_dim; ++o) {
                back += output_grad[o] * model->output_weights[o * hidden_dim + h];
            }
            hidden_grad[h] = back * (1.0 - hidden[h] * hidden[h]);
        }

        for (size_t o = 0; o < output_dim; ++o) {
            size_t offset = o * hidden_dim;
            for (size_t h = 0; h < hidden_dim; ++h) {
                model->output_weights[offset + h] -= learning_rate * (output_grad[o] * hidden[h]);
            }
            model->output_bias[o] -= learning_rate * output_grad[o];
        }

        for (size_t h = 0; h < hidden_dim; ++h) {
            size_t offset = h * input_dim;
            for (size_t i = 0; i < input_dim; ++i) {
                model->input_weights[offset + i] -= learning_rate * (hidden_grad[h] * features[i]);
            }
            model->hidden_bias[h] -= learning_rate * hidden_grad[h];
        }
    }

    free(features);
    free(hidden);
    free(hidden_grad);
    free(outputs);
    free(output_grad);

    return effective_batch > 0 ? total_loss / (double)effective_batch : -1.0;
}

static double transformer_model_train_batch(FormulaTrainingPipeline *pipeline,
                                            size_t cursor,
                                            size_t batch_size,
                                            double learning_rate) {
    if (!pipeline || batch_size == 0) {
        return -1.0;
    }

    FormulaTransformerModel *model = &pipeline->transformer_model;
    const FormulaDataset *dataset = &pipeline->dataset;
    if (!model->w_q || dataset->count == 0) {
        return -1.0;
    }

    size_t dataset_count = dataset->count;
    size_t input_dim = model->input_dim;
    size_t model_dim = model->model_dim;
    size_t effective_batch = batch_size > dataset_count ? dataset_count : batch_size;
    size_t start = dataset_count ? cursor % dataset_count : 0;

    double *features = calloc(input_dim, sizeof(double));
    double *q = calloc(model_dim, sizeof(double));
    double *k = calloc(model_dim, sizeof(double));
    double *v = calloc(model_dim, sizeof(double));
    double *context = calloc(model_dim, sizeof(double));
    double *dcontext = calloc(model_dim, sizeof(double));
    double *dq = calloc(model_dim, sizeof(double));
    double *dk = calloc(model_dim, sizeof(double));
    double *dv = calloc(model_dim, sizeof(double));
    double *grad_w_o = calloc(model_dim, sizeof(double));
    if (!features || !q || !k || !v || !context || !dcontext || !dq || !dk || !dv || !grad_w_o) {
        free(features);
        free(q);
        free(k);
        free(v);
        free(context);
        free(dcontext);
        free(dq);
        free(dk);
        free(dv);
        free(grad_w_o);
        return -1.0;
    }

    double total_loss = 0.0;
    double scale = model_dim > 0 ? 1.0 / sqrt((double)model_dim) : 1.0;

    for (size_t b = 0; b < effective_batch; ++b) {
        size_t index = (start + b) % dataset_count;
        const FormulaDatasetEntry *entry = &dataset->entries[index];
        dataset_entry_features(entry, features, input_dim);

        for (size_t m = 0; m < model_dim; ++m) {
            size_t offset = m * input_dim;
            double q_sum = 0.0;
            double k_sum = 0.0;
            double v_sum = 0.0;
            for (size_t i = 0; i < input_dim; ++i) {
                double feature = features[i];
                q_sum += model->w_q[offset + i] * feature;
                k_sum += model->w_k[offset + i] * feature;
                v_sum += model->w_v[offset + i] * feature;
            }
            q[m] = q_sum;
            k[m] = k_sum;
            v[m] = v_sum;
        }

        double dot = 0.0;
        for (size_t m = 0; m < model_dim; ++m) {
            dot += q[m] * k[m];
        }
        double score = dot * scale;
        score = clamp_double(score, -50.0, 50.0);
        double attn = 1.0 / (1.0 + exp(-score));

        for (size_t m = 0; m < model_dim; ++m) {
            context[m] = attn * v[m];
        }

        double output = model->bias;
        for (size_t m = 0; m < model_dim; ++m) {
            output += model->w_o[m] * context[m];
        }

        double target = entry ? entry->effectiveness : 0.0;
        double error = output - target;
        total_loss += 0.5 * error * error;

        for (size_t m = 0; m < model_dim; ++m) {
            grad_w_o[m] = error * context[m];
            dcontext[m] = error * model->w_o[m];
        }

        double dattention = 0.0;
        for (size_t m = 0; m < model_dim; ++m) {
            dv[m] = dcontext[m] * attn;
            dattention += dcontext[m] * v[m];
        }

        double dscore = dattention * attn * (1.0 - attn);
        for (size_t m = 0; m < model_dim; ++m) {
            dq[m] = dscore * k[m] * scale;
            dk[m] = dscore * q[m] * scale;
        }

        for (size_t m = 0; m < model_dim; ++m) {
            model->w_o[m] -= learning_rate * grad_w_o[m];
        }
        model->bias -= learning_rate * error;

        for (size_t m = 0; m < model_dim; ++m) {
            size_t offset = m * input_dim;
            double grad_v = dv[m];
            double grad_q = dq[m];
            double grad_k = dk[m];
            for (size_t i = 0; i < input_dim; ++i) {
                double feature = features[i];
                model->w_v[offset + i] -= learning_rate * (grad_v * feature);
                model->w_q[offset + i] -= learning_rate * (grad_q * feature);
                model->w_k[offset + i] -= learning_rate * (grad_k * feature);
            }
        }
    }

    free(features);
    free(q);
    free(k);
    free(v);
    free(context);
    free(dcontext);
    free(dq);
    free(dk);
    free(dv);
    free(grad_w_o);

    return effective_batch > 0 ? total_loss / (double)effective_batch : -1.0;
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

    ai->iterations = 0;
    ai->average_reward = 0.0;
    ai->exploration_rate = 0.4;
    ai->exploitation_rate = 0.6;

    ai->training_interval = 20;
    ai->transformer_interval = 45;
    ai->training_batch_size = 4;
    ai->transformer_batch_size = 4;
    ai->mlp_learning_rate = 0.05;
    ai->transformer_learning_rate = 0.03;
    ai->training_cursor = 0;
    ai->transformer_cursor = 0;

    if (access("cfg/learning_data.json", R_OK) == 0) {
        formula_training_pipeline_load_dataset(ai->pipeline, "cfg/learning_data.json");
    }
    if (access("cfg/mlp_weights.bin", R_OK) == 0) {
        formula_training_pipeline_load_weights(ai->pipeline, "cfg/mlp_weights.bin");
    }

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

    int weights_dirty = 0;
    if (ai->pipeline && ai->pipeline->dataset.count > 0) {
        size_t dataset_count = ai->pipeline->dataset.count;
        if (ai->training_interval > 0 && dataset_count > 0 &&
            ai->iterations % ai->training_interval == 0) {
            double loss = mlp_model_train_batch(ai->pipeline,
                                                ai->training_cursor,
                                                ai->training_batch_size,
                                                ai->mlp_learning_rate);
            if (loss >= 0.0) {
                ai->pipeline->metrics.last_mlp_loss = loss;
                ai->pipeline->metrics.total_training_steps++;
                size_t advance = dataset_count ? ai->training_batch_size % dataset_count : 0;
                if (advance == 0 && dataset_count > 0) {
                    advance = 1;
                }
                if (dataset_count > 0) {
                    ai->training_cursor = (ai->training_cursor + advance) % dataset_count;
                }
                weights_dirty = 1;
            }
        }

        if (ai->transformer_interval > 0 && dataset_count > 0 &&
            ai->iterations % ai->transformer_interval == 0) {
            double loss = transformer_model_train_batch(ai->pipeline,
                                                       ai->transformer_cursor,
                                                       ai->transformer_batch_size,
                                                       ai->transformer_learning_rate);
            if (loss >= 0.0) {
                ai->pipeline->metrics.last_transformer_loss = loss;
                ai->pipeline->metrics.total_training_steps++;
                size_t advance = dataset_count ? ai->transformer_batch_size % dataset_count : 0;
                if (advance == 0 && dataset_count > 0) {
                    advance = 1;
                }
                if (dataset_count > 0) {
                    ai->transformer_cursor = (ai->transformer_cursor + advance) % dataset_count;
                }
                weights_dirty = 1;
            }
        }
    }

    if (weights_dirty && ai->pipeline) {
        formula_training_pipeline_sync_weights_buffer(ai->pipeline);
    }

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
