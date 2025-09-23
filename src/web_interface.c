#include <microhttpd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <json-c/json.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

// Флаг для корректного завершения демона по сигналу
static volatile sig_atomic_t server_stop = 0;

static void handle_sig(int sig) {
    (void)sig;
    server_stop = 1;
}

// Mutex to protect file and model writes
static pthread_mutex_t storage_mutex = PTHREAD_MUTEX_INITIALIZER;
// Server start time
static time_t server_start_time = 0;

// Thread-safe logging to /tmp/web_interface.log
static void log_server_event(const char *fmt, ...) {
    pthread_mutex_lock(&storage_mutex);
    FILE *f = fopen("/tmp/web_interface.log", "a");
    if (f) {
        time_t now = time(NULL);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(f, "[%s] ", ts);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fprintf(f, "\n");
        fclose(f);
    }
    pthread_mutex_unlock(&storage_mutex);
}
#include "abstract_mind.h"
#include "brain.h"
#include "tfidf.h"
#include "embed.h"
#include "mlp.h"
#include "node_brain.h"
#include "rule_engine.h"
#include "arithmetic.h"

#define PORT 8888
#define MAX_BUFFER 4096
#define LEARNING_FILE "learning_data.json"
#define METRICS_FILE "learning_metrics.json"
#define MIN_SIMILARITY_THRESHOLD 0.6

// Структура для хранения обучающих данных
struct LearningData {
    char task[MAX_BUFFER];
    char response[MAX_BUFFER];
    float effectiveness;
    int rating;
    time_t timestamp;
    float complexity;
    int cluster_id;
    char used_rule_id[64];
};

// Структура для метрик обучения
struct LearningMetrics {
    int total_tasks;
    int successful_tasks;
    float avg_rating;
    float avg_effectiveness;
    time_t last_update;
    int total_clusters;
};

// Вычисление сходства между задачами
static float calculate_similarity(const char* task1, const char* task2) {
    // Используем косинусную меру на базе токенов (tfidf.c)
    double sim = tfidf_cosine_similarity(task1 ? task1 : "", task2 ? task2 : "");
    // Если по каким-то причинам sim==0, можно использовать запасной метод
    if (sim <= 0.0) {
        // fallback: простой подстрочный метод (сохраним поведение)
        size_t len1 = strlen(task1);
        size_t len2 = strlen(task2);
        size_t max_len = len1 > len2 ? len1 : len2;
        if (max_len == 0) return 0.0f;
        size_t common = 0;
        for (size_t i = 0; i < len1; i++) {
            for (size_t j = 0; j < len2; j++) {
                size_t k = 0;
                while (i + k < len1 && j + k < len2 && task1[i + k] == task2[j + k]) k++;
                if (k > 2) { common += k; i += k - 1; break; }
            }
        }
        return (float)common / max_len;
    }
    return (float)sim;
}

// Вычисление сложности задачи
static float calculate_task_complexity(const char* task) {
    size_t len = strlen(task);
    int special_chars = 0;
    int numbers = 0;
    int words = 1; // Начинаем с 1, так как последнее слово может не иметь пробела после
    
    for (size_t i = 0; i < len; i++) {
        if (task[i] == ' ') words++;
        else if (strchr("+-*/^()[]{}=", task[i])) special_chars++;
        else if (isdigit(task[i])) numbers++;
    }
    
    // Нормализованная сложность от 0 до 1
    float complexity = (float)(special_chars * 2 + numbers + words) / (len * 2);
    return complexity > 1.0f ? 1.0f : complexity;
}

// Безопасное декодирование URL-кодированных строк (percent-decoding)
static void url_decode(const char *src, char *dst, size_t dst_size) {
    size_t di = 0;
    while (*src && di + 1 < dst_size) {
        if (*src == '+') {
            dst[di++] = ' ';
            src++;
        } else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            dst[di++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            dst[di++] = *src++;
        }
    }
    dst[di] = '\0';
}

// Read file into buffer (caller must free). Returns 0 on success.
static int read_whole_file(const char *path, char **out_buf, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t r = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[r] = '\0';
    *out_buf = buf;
    *out_size = r;
    return 0;
}

static void guess_mime(const char *path, char *out, size_t out_len) {
    const char *ext = strrchr(path, '.');
    if (!ext) { strncpy(out, "application/octet-stream", out_len); return; }
    if (strcmp(ext, ".html") == 0) strncpy(out, "text/html; charset=UTF-8", out_len);
    else if (strcmp(ext, ".js") == 0) strncpy(out, "application/javascript; charset=UTF-8", out_len);
    else if (strcmp(ext, ".css") == 0) strncpy(out, "text/css; charset=UTF-8", out_len);
    else if (strcmp(ext, ".json") == 0) strncpy(out, "application/json; charset=UTF-8", out_len);
    else if (strcmp(ext, ".png") == 0) strncpy(out, "image/png", out_len);
    else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) strncpy(out, "image/jpeg", out_len);
    else if (strcmp(ext, ".svg") == 0) strncpy(out, "image/svg+xml", out_len);
    else strncpy(out, "application/octet-stream", out_len);
}


// Arithmetic evaluator moved to src/arithmetic.c

// Структура для хранения POST данных
    struct PostContext {
    char buffer[MAX_BUFFER];
    size_t pos;
    int processed;
    char *decoded_task;  // Добавляем поле для хранения декодированного текста
};

// Обновление метрик обучения
static void update_learning_metrics(const struct LearningData* new_data) {
    // Убедимся, что файл метрик существует
    FILE* fcheck = fopen(METRICS_FILE, "r");
    if (!fcheck) {
        json_object* init = json_object_new_object();
        json_object_object_add(init, "total_tasks", json_object_new_int(0));
        json_object_object_add(init, "successful_tasks", json_object_new_int(0));
        json_object_object_add(init, "avg_rating", json_object_new_double(0.0));
        json_object_object_add(init, "avg_effectiveness", json_object_new_double(0.0));
        json_object_object_add(init, "total_clusters", json_object_new_int(0));
        json_object_object_add(init, "last_update", json_object_new_int64(time(NULL)));
        json_object_to_file(METRICS_FILE, init);
        json_object_put(init);
    } else { fclose(fcheck); }

    json_object* root = json_object_from_file(METRICS_FILE);
    struct LearningMetrics metrics = {0};
    
    if (root) {
        json_object* j_total_tasks, *j_successful_tasks, *j_avg_rating,
                    *j_avg_effectiveness, *j_total_clusters;
                    
        json_object_object_get_ex(root, "total_tasks", &j_total_tasks);
        json_object_object_get_ex(root, "successful_tasks", &j_successful_tasks);
        json_object_object_get_ex(root, "avg_rating", &j_avg_rating);
        json_object_object_get_ex(root, "avg_effectiveness", &j_avg_effectiveness);
        json_object_object_get_ex(root, "total_clusters", &j_total_clusters);
        
        metrics.total_tasks = json_object_get_int(j_total_tasks);
        metrics.successful_tasks = json_object_get_int(j_successful_tasks);
        metrics.avg_rating = json_object_get_double(j_avg_rating);
        metrics.avg_effectiveness = json_object_get_double(j_avg_effectiveness);
        metrics.total_clusters = json_object_get_int(j_total_clusters);
        
        json_object_put(root);
    }
    
    // Обновляем метрики
    metrics.total_tasks++;
    if (new_data->rating >= 4) metrics.successful_tasks++;
    metrics.avg_rating = (metrics.avg_rating * (metrics.total_tasks - 1) + new_data->rating) 
                        / metrics.total_tasks;
    metrics.avg_effectiveness = (metrics.avg_effectiveness * (metrics.total_tasks - 1) + 
                               new_data->effectiveness) / metrics.total_tasks;
    metrics.last_update = time(NULL);
    
    // Сохраняем обновленные метрики
    root = json_object_new_object();
    json_object_object_add(root, "total_tasks", json_object_new_int(metrics.total_tasks));
    json_object_object_add(root, "successful_tasks", json_object_new_int(metrics.successful_tasks));
    json_object_object_add(root, "avg_rating", json_object_new_double(metrics.avg_rating));
    json_object_object_add(root, "avg_effectiveness", json_object_new_double(metrics.avg_effectiveness));
    json_object_object_add(root, "total_clusters", json_object_new_int(metrics.total_clusters));
    json_object_object_add(root, "last_update", json_object_new_int64(metrics.last_update));
    
    json_object_to_file(METRICS_FILE, root);
    json_object_put(root);
}

// Сохранение данных обучения
static void save_learning_data(const struct LearningData* data) {
    pthread_mutex_lock(&storage_mutex);
    // Убедимся, что файл обучения существует
    FILE* fcheck = fopen(LEARNING_FILE, "r");
    if (!fcheck) {
        json_object* empty = json_object_new_array();
        json_object_to_file(LEARNING_FILE, empty);
        json_object_put(empty);
    } else { fclose(fcheck); }

    json_object* root = json_object_from_file(LEARNING_FILE);
    if (!root) {
        root = json_object_new_array();
    }

    // Определяем кластер для новой задачи
    int cluster_id = 0;
    float max_similarity = 0;
    size_t n_entries = json_object_array_length(root);
    
    for (size_t i = 0; i < n_entries; i++) {
        json_object* entry = json_object_array_get_idx(root, i);
        json_object* j_task, *j_cluster_id;
        
        json_object_object_get_ex(entry, "task", &j_task);
        json_object_object_get_ex(entry, "cluster_id", &j_cluster_id);
        
        float similarity = calculate_similarity(data->task, 
            json_object_get_string(j_task));
            
        if (similarity > max_similarity) {
            max_similarity = similarity;
            if (j_cluster_id) {
                cluster_id = json_object_get_int(j_cluster_id);
            }
        }
    }
    
    // Если нет похожих задач, создаем новый кластер
    if (max_similarity < MIN_SIMILARITY_THRESHOLD) {
        json_object* metrics_obj = json_object_from_file(METRICS_FILE);
        if (metrics_obj) {
            json_object* j_total_clusters;
            json_object_object_get_ex(metrics_obj, "total_clusters", &j_total_clusters);
            cluster_id = json_object_get_int(j_total_clusters) + 1;
            json_object_put(metrics_obj);
        }
    }

    json_object* entry = json_object_new_object();
    json_object_object_add(entry, "task", json_object_new_string(data->task));
    json_object_object_add(entry, "response", json_object_new_string(data->response));
    json_object_object_add(entry, "effectiveness", json_object_new_double(data->effectiveness));
    json_object_object_add(entry, "rating", json_object_new_int(data->rating));
    json_object_object_add(entry, "timestamp", json_object_new_int64(data->timestamp));
    json_object_object_add(entry, "complexity", json_object_new_double(data->complexity));
    json_object_object_add(entry, "cluster_id", json_object_new_int(cluster_id));
    if (data->used_rule_id[0] != '\0') json_object_object_add(entry, "used_rule_id", json_object_new_string(data->used_rule_id));

    json_object_array_add(root, entry);
    json_object_to_file(LEARNING_FILE, root);
    json_object_put(root);
    
    // Обновляем метрики обучения
    update_learning_metrics(data);
    pthread_mutex_unlock(&storage_mutex);
}

// Анализ предыдущих ответов для улучшения текущего
static void analyze_previous_responses(const char* task, Formula* formula) {
    json_object* root = json_object_from_file(LEARNING_FILE);
    if (!root) return;

    size_t n_entries = json_object_array_length(root);
    double total_effectiveness = 0;
    double total_complexity = 0;
    int count = 0;
    float best_cluster_score = 0;
    (void)best_cluster_score; // может быть использовано в дальнейшем анализе
    
    // Вычисляем сложность текущей задачи
    float current_complexity = calculate_task_complexity(task);
    
    // Поиск наиболее подходящего кластера
    for (size_t i = 0; i < n_entries; i++) {
        json_object* entry = json_object_array_get_idx(root, i);
        json_object *j_task, *j_effectiveness, *j_rating, 
                   *j_complexity, *j_cluster_id;

        json_object_object_get_ex(entry, "task", &j_task);
        json_object_object_get_ex(entry, "effectiveness", &j_effectiveness);
        json_object_object_get_ex(entry, "rating", &j_rating);
        json_object_object_get_ex(entry, "complexity", &j_complexity);
        json_object_object_get_ex(entry, "cluster_id", &j_cluster_id);

        float similarity = calculate_similarity(task, 
            json_object_get_string(j_task));
        float complexity_diff = fabs(current_complexity - 
            json_object_get_double(j_complexity));
            
        if (similarity > MIN_SIMILARITY_THRESHOLD) {
            int rating = json_object_get_int(j_rating);
            int cluster_id = 0;
            if (j_cluster_id) cluster_id = json_object_get_int(j_cluster_id);
            (void)cluster_id;
            
            // Оценка кластера
            float cluster_score = similarity * (1.0f - complexity_diff) * rating;
            if (cluster_score > best_cluster_score) {
                best_cluster_score = cluster_score;
            }
            
            if (rating >= 4) { // Учитываем только хорошие решения
                double effectiveness = json_object_get_double(j_effectiveness);
                double complexity = json_object_get_double(j_complexity);
                
                // Взвешиваем эффективность с учетом сложности
                total_effectiveness += effectiveness * (1.0 - complexity_diff);
                total_complexity += complexity;
                count++;
            }
        }
    }

    if (count > 0) {
        // Корректируем эффективность формулы на основе предыдущего опыта
        // и сложности задачи
        double avg_complexity = total_complexity / count;
        double complexity_factor = 1.0 + (current_complexity - avg_complexity);
        
        formula->effectiveness = (formula->effectiveness + 
            (total_effectiveness / count)) / 2 * complexity_factor;
            
        // Ограничиваем эффективность в пределах [0, 1]
        if (formula->effectiveness > 1.0) formula->effectiveness = 1.0;
        else if (formula->effectiveness < 0.0) formula->effectiveness = 0.0;
    }

    json_object_put(root);
}

// Обработка задачи через abstract_mind
// Node brain instance for this process
static NodeBrain node_brain;
static RuleEngine rule_engine;
static pthread_t rule_gc_thread;
static volatile int rule_gc_stop = 0;
static pthread_t trainer_thread;
static volatile int trainer_stop = 0;

static void* rule_gc_worker(void *arg) {
    RuleEngine *re = (RuleEngine*)arg;
    while (!rule_gc_stop) {
        sleep(30); // run every 30s by default
        pthread_mutex_lock(&storage_mutex);
        int removed = rule_engine_gc(re, 3, 0.2); // prune rules with >=3 hits and success_rate<20%
        if (removed > 0) log_server_event("rule_gc removed %d rules", removed);
        pthread_mutex_unlock(&storage_mutex);
    }
    return NULL;
}

// Background incremental trainer: reads learning entries and updates MLP
static void* trainer_worker(void *arg) {
    (void)arg;
    while (!trainer_stop) {
        sleep(10); // run every 10s
        pthread_mutex_lock(&storage_mutex);
        json_object *root = json_object_from_file(LEARNING_FILE);
        if (!root) {
            pthread_mutex_unlock(&storage_mutex);
            continue;
        }
        size_t n = json_object_array_length(root);
        int changed = 0;
        for (size_t i = 0; i < n; ++i) {
            json_object *entry = json_object_array_get_idx(root, i);
            if (!entry) continue;
            json_object *jtrained = NULL;
            if (json_object_object_get_ex(entry, "trained", &jtrained)) continue; // already trained
            json_object *jrating = NULL;
            if (!json_object_object_get_ex(entry, "rating", &jrating)) continue;
            int rating = json_object_get_int(jrating);
            if (rating <= 0) continue;
            json_object *jtask = NULL;
            if (!json_object_object_get_ex(entry, "task", &jtask)) continue;
            const char *task = json_object_get_string(jtask);
            if (!task) continue;

            const size_t EMB_DIM = 64;
            double emb[EMB_DIM];
            embed_text(task, emb, EMB_DIM);
            double mean = 0.0, mx = -1e9;
            for (size_t k = 0; k < EMB_DIM; ++k) { mean += emb[k]; if (emb[k] > mx) mx = emb[k]; }
            mean /= (double)EMB_DIM;
            double len_norm = (double)strlen(task) / (double)MAX_BUFFER;
            double features_small[3] = { mean < 0.0 ? 0.0 : mean, mx < 0.0 ? 0.0 : mx, len_norm };
            for (int fi = 0; fi < 3; ++fi) { if (features_small[fi] < 0.0) features_small[fi] = 0.0; if (features_small[fi] > 1.0) features_small[fi] = 1.0; }

            double target = (double)rating / 5.0;
            mlp_update(features_small, target, 0.005);

            json_object_object_add(entry, "trained", json_object_new_int(1));
            changed = 1;
        }
        if (changed) {
            json_object_to_file(LEARNING_FILE, root);
            if (mlp_save("mlp_weights.bin") != 0) {
                log_server_event("Warning: mlp_save failed in trainer");
            }
        }
        json_object_put(root);
        pthread_mutex_unlock(&storage_mutex);
    }
    return NULL;
}

static const char* process_task(const char* task) {
    static char result[1024];
    // Быстрый путь: арифметические выражения
    char arbuf[256];
    if (evaluate_arithmetic(task, arbuf, sizeof(arbuf))) {
        // Save to learning store
        struct LearningData learning_data = {0};
        learning_data.effectiveness = 1.0f; // deterministic
        learning_data.rating = 0;
        learning_data.timestamp = time(NULL);
        learning_data.complexity = calculate_task_complexity(task);
        strncpy(learning_data.task, task, sizeof(learning_data.task) - 1);
        strncpy(learning_data.response, arbuf, sizeof(learning_data.response) - 1);
        learning_data.used_rule_id[0] = '\0';
        save_learning_data(&learning_data);
        snprintf(result, sizeof(result), "%s", arbuf);
        return result;
    }
    // Let node brain attempt to process the task first
    const char *nb_res = node_brain_process(&node_brain, task, result, sizeof(result));
    if (nb_res) {
        // Also save into learning store a compact record
        struct LearningData learning_data = {0};
        learning_data.effectiveness = 0.5f; // default guessed
        learning_data.rating = 0;
        learning_data.timestamp = time(NULL);
        learning_data.complexity = calculate_task_complexity(task);
        strncpy(learning_data.task, task, sizeof(learning_data.task) - 1);
        strncpy(learning_data.response, result, sizeof(learning_data.response) - 1);
        learning_data.used_rule_id[0] = '\0';
        save_learning_data(&learning_data);
        return result;
    }
    // Check rule engine for match before heavy pipeline
    json_object *rule = NULL;
    pthread_mutex_lock(&storage_mutex);
    rule = rule_engine_find_best_match(&rule_engine, task);
    if (rule) {
        json_object *jexpr = NULL, *jid = NULL;
        const char *expr = NULL;
        const char *rid = NULL;
        if (json_object_object_get_ex(rule, "expr", &jexpr)) expr = json_object_get_string(jexpr);
        if (json_object_object_get_ex(rule, "id", &jid)) rid = json_object_get_string(jid);
        if (expr) snprintf(result, sizeof(result), "Правило: %s", expr);
        else snprintf(result, sizeof(result), "Правило сработало");
        // record hit
        if (rid) rule_engine_record_hit(&rule_engine, rid);
        pthread_mutex_unlock(&storage_mutex);

        struct LearningData learning_data = {0};
        learning_data.effectiveness = 0.8f; // assume decent when rule applied
        learning_data.rating = 0;
        learning_data.timestamp = time(NULL);
        learning_data.complexity = calculate_task_complexity(task);
        strncpy(learning_data.task, task, sizeof(learning_data.task) - 1);
        strncpy(learning_data.response, result, sizeof(learning_data.response) - 1);
        if (rid) strncpy(learning_data.used_rule_id, rid, sizeof(learning_data.used_rule_id) - 1);
        else learning_data.used_rule_id[0] = '\0';
        save_learning_data(&learning_data);
        json_object_put(rule);
        return result;
    }
    pthread_mutex_unlock(&storage_mutex);
    // Fallback to previous complex pipeline if needed
    static char fallback[1024];
    Formula formula = generate_formula();
    snprintf(formula.expression, sizeof(formula.expression), "%s", task);
    float task_complexity = calculate_task_complexity(task);
    analyze_previous_responses(task, &formula);
    formula = evolve_formula(formula);
    const size_t EMB_DIM = 64;
    double emb[EMB_DIM];
    embed_text(task, emb, EMB_DIM);
    double mean = 0.0, mx = -1e9;
    for (size_t i = 0; i < EMB_DIM; ++i) { mean += emb[i]; if (emb[i] > mx) mx = emb[i]; }
    mean /= (double)EMB_DIM;
    double len_norm = (double)strlen(task) / (double)MAX_BUFFER;
    double features_small[3] = { mean < 0.0 ? 0.0 : mean, mx < 0.0 ? 0.0 : mx, len_norm };
    for (int fi = 0; fi < 3; ++fi) { if (features_small[fi] < 0.0) features_small[fi] = 0.0; if (features_small[fi] > 1.0) features_small[fi] = 1.0; }
    double net_pred = mlp_predict(features_small);
    formula.effectiveness = (formula.effectiveness + net_pred) / 2.0;
    snprintf(fallback, sizeof(fallback), "Результат: %s (Эффективность: %.2f%%, Сложность: %.2f)", formula.expression, formula.effectiveness * 100, task_complexity * 100);
    struct LearningData learning_data = {0};
    learning_data.effectiveness = formula.effectiveness;
    learning_data.rating = 0;
    learning_data.timestamp = time(NULL);
    learning_data.complexity = task_complexity;
    strncpy(learning_data.task, task, sizeof(learning_data.task) - 1);
    strncpy(learning_data.response, fallback, sizeof(learning_data.response) - 1);
    save_learning_data(&learning_data);
    return fallback;
}

// Логирование задач и результатов
static void log_task(const char* task, const char* result) {
    FILE* log_file = fopen("task_log.txt", "a");
    if (log_file) {
        time_t now = time(NULL);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(log_file, "[%s] Задача: %s | Ответ: %s\n", timestamp, task, result);
        fclose(log_file);
    }
}

// HTML страница с интерфейсом
// HTML страница как строка C
static const char* html_page = 
"<!DOCTYPE html>\n"
"<html lang=\"ru\">\n"
"<head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"    <title>Kolibri AI - Интерактивный разум</title>\n"
"    <style>\n"
"        body { font-family: Arial, sans-serif; background: #f0f2f5; margin: 0; padding: 20px; }\n"
"        .container { background: white; max-width: 800px; margin: 0 auto; padding: 30px; border-radius: 12px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n"
"        h1 { color: #1a73e8; margin: 0 0 20px 0; font-size: 28px; }\n"
"        .input-group { margin-bottom: 20px; }\n"
"        label { display: block; margin-bottom: 8px; color: #444; font-weight: 500; }\n"
"        input[type=text] { width: 100%; padding: 12px; border: 2px solid #dfe1e5; border-radius: 8px; font-size: 16px; box-sizing: border-box; }\n"
"        button { background: #1a73e8; color: white; border: none; padding: 12px 24px; border-radius: 8px; font-size: 16px; cursor: pointer; width: 100%; }\n"
"        #response { margin-top: 20px; padding: 15px; border-radius: 8px; display: none; }\n"
"        #response.success { background: #e6f4ea; border: 1px solid #34a853; }\n"
"        #response.error { background: #fce8e6; border: 1px solid #ea4335; }\n"
"        #feedback { margin-top: 20px; padding-top: 20px; border-top: 1px solid #dfe1e5; display: none; }\n"
"        select { width: 100%; padding: 12px; border: 2px solid #dfe1e5; border-radius: 8px; font-size: 16px; margin-bottom: 10px; }\n"
"    </style>\n"
"    <script>\n"
"        function showElement(id) {\n"
"            document.getElementById(id).style.display = 'block';\n"
"        }\n"
"        function hideElement(id) {\n"
"            document.getElementById(id).style.display = 'none';\n"
"        }\n"
"        function setResponse(text, isError) {\n"
"            var resp = document.getElementById('response');\n"
"            resp.textContent = text;\n"
"            resp.className = isError ? 'error' : 'success';\n"
"            resp.style.display = 'block';\n"
"            if (!isError) showElement('feedback');\n"
"        }\n"
"        function sendTask() {\n"
"            var task = document.getElementById('task').value.trim();\n"
"            if (!task) {\n"
"                setResponse('Пожалуйста, введите задачу', true);\n"
"                return;\n"
"            }\n"
"            fetch('/', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/x-www-form-urlencoded'},\n"
"                body: 'task=' + encodeURIComponent(task)\n"
"            })\n"
"            .then(function(r) { return r.text(); })\n"
"            .then(function(text) { setResponse(text, false); })\n"
"            .catch(function(error) { setResponse('Ошибка: ' + error, true); });\n"
"        }\n"
"        function sendFeedback() {\n"
"            var rating = document.getElementById('rating').value;\n"
"            fetch('/', {\n"
"                method: 'POST',\n"
"                headers: {'Content-Type': 'application/x-www-form-urlencoded'},\n"
"                body: 'feedback=' + rating\n"
"            })\n"
"            .then(function(r) { return r.text(); })\n"
"            .then(function(text) {\n"
"                setResponse(text, false);\n"
"                hideElement('feedback');\n"
"            })\n"
"            .catch(function(error) { setResponse('Ошибка: ' + error, true); });\n"
"        }\n"
"    </script>\n"
"</head>\n"
"<body>\n"
"    <div class=\"container\">\n"
"        <h1>Kolibri AI: Интерактивный разум</h1>\n"
"        <div class=\"input-group\">\n"
"            <label for=\"task\">Введите вашу задачу:</label>\n"
"            <input type=\"text\" id=\"task\" placeholder=\"Опишите задачу для искусственного интеллекта...\">\n"
"            <button onclick=\"sendTask()\">Отправить задачу ➤</button>\n"
"        </div>\n"
"        <div id=\"response\"></div>\n"
"        <div id=\"feedback\">\n"
"            <label for=\"rating\">Оцените качество ответа:</label>\n"
"            <select id=\"rating\">\n"
"                <option value=\"5\">★★★★★ Отлично - полное решение</option>\n"
"                <option value=\"4\">★★★★☆ Хорошо - почти полное решение</option>\n"
"                <option value=\"3\">★★★☆☆ Нормально - частичное решение</option>\n"
"                <option value=\"2\">★★☆☆☆ Плохо - много ошибок</option>\n"
"                <option value=\"1\">★☆☆☆☆ Совсем плохо - нет решения</option>\n"
"            </select>\n"
"            <button onclick=\"sendFeedback()\">Отправить оценку</button>\n"
"        </div>\n"
"    </div>\n"
"    <div class=\"container\">\n"
"        <h2>История (последние 10)</h2>\n"
"        <div id=\"history\">Загрузка...</div>\n"
"        <button onclick=\"refreshHistory()\">Обновить историю</button>\n"
"    </div>\n"
"    <script>\n"
"    function refreshHistory() {\n"
"        fetch('/api/v1/tasks?limit=10').then(r=>r.json()).then(json=>{\n"
"            var h = document.getElementById('history');\n"
"            if (!json || json.length==0) { h.textContent='Пусто'; return; }\n"
"            var html = '<ol>';\n"
"            for (var i=0;i<json.length;i++) {\n"
"                var e = json[i];\n"
"                html += '<li><b>'+ (e.task||'') +'</b> -> '+ (e.response||'') + ' (' + ((e.effectiveness||0)*100).toFixed(1) + '%)';\n"
"                if (e.used_rule_id) html += ' [rule:'+e.used_rule_id+']';\n"
"                html += '</li>';\n"
"            }\n"
"            html += '</ol>'; h.innerHTML = html;\n"
"        }).catch(e=>{ document.getElementById('history').textContent='Ошибка: '+e; });\n"
"    }\n"
"    window.onload = function(){ refreshHistory(); };\n"
"    </script>\n"
"</body>\n"
"</html>";
// Оставляем только точку с запятой для закрытия объявления html_page
;

// Обработка запросов (переписанная, упрощённая и корректно сбалансированная)
static enum MHD_Result request_handler(void *cls __attribute__((unused)), 
                                     struct MHD_Connection *connection,
                                     const char *url, const char *method, 
                                     const char *version __attribute__((unused)),
                                     const char *upload_data, size_t *upload_data_size, void **con_cls) {
    struct MHD_Response *response = NULL;
    struct PostContext *post_ctx = NULL;
    int ret = MHD_NO;

    // Log incoming request
    log_server_event("REQ %s %s", method ? method : "?", url ? url : "?");

    // Handle CORS preflight
    if (0 == strcmp(method, "OPTIONS")) {
        const char ok[] = "OK";
        response = MHD_create_response_from_buffer(strlen(ok), (void*)ok, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    /* Only allocate connection context for POST bodies. For GET/HEAD we handle immediately. */
    if (NULL == *con_cls) {
        if (0 == strcmp(method, "POST")) {
            post_ctx = calloc(1, sizeof(struct PostContext));
            if (!post_ctx) return MHD_NO;
            *con_cls = post_ctx;
            return MHD_YES;
        }
    }
    post_ctx = *con_cls;

    // Serve static files under /web/ and root index
    if (0 == strcmp(method, "GET") && url && (strcmp(url, "/") == 0 || strncmp(url, "/web/", 5) == 0)) {
        char pathbuf[1024];
        const char *rel = NULL;
        if (strcmp(url, "/") == 0) rel = "web/index.html";
        else rel = url + 1; // strip leading '/'
        if (strstr(rel, "..")) return MHD_NO;
        // try multiple candidate locations depending on working dir
        const char *cands[] = { rel, (char[]){'.','/','\0'} , NULL };
        // build candidates: rel, ./rel, ../rel
        char cand1[1024]; char cand2[1024]; char cand3[1024];
        snprintf(cand1, sizeof(cand1), "%s", rel);
        snprintf(cand2, sizeof(cand2), "./%s", rel);
        snprintf(cand3, sizeof(cand3), "../%s", rel);
        char *filebuf = NULL; size_t fsize = 0;
        if (read_whole_file(cand1, &filebuf, &fsize) != 0) {
            if (read_whole_file(cand2, &filebuf, &fsize) != 0) {
                if (read_whole_file(cand3, &filebuf, &fsize) != 0) {
                    filebuf = NULL; fsize = 0;
                }
            }
        }
        if (filebuf) {
            char mime[128] = "application/octet-stream";
            guess_mime(rel, mime, sizeof(mime));
            response = MHD_create_response_from_buffer((size_t)fsize, (void*)filebuf, MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(response, "Content-Type", mime);
            MHD_add_response_header(response, "Cache-Control", "max-age=60");
            ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);
            return ret;
        }
        // fallthrough to API if not found
    }

    // SSE events endpoint
    if (0 == strcmp(method, "GET") && url && strcmp(url, "/api/v1/events") == 0) {
        // simple single-event response (clients should reconnect)
        char buf[512];
        time_t now = time(NULL);
        snprintf(buf, sizeof(buf), "data: {\"uptime\":%lld}\n\n", (long long)(now - server_start_time));
        response = MHD_create_response_from_buffer(strlen(buf), (void*)buf, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/event-stream; charset=UTF-8");
        MHD_add_response_header(response, "Cache-Control", "no-cache");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    // Быстрые JSON API endpoints
    if (0 == strcmp(method, "GET") && url && strcmp(url, "/api/v1/status") == 0) {
        json_object *j = json_object_new_object();
        time_t now = time(NULL);
        json_object_object_add(j, "uptime", json_object_new_int64((int64_t)(now - server_start_time)));
        json_object_object_add(j, "pid", json_object_new_int((int)getpid()));
        const char *s = json_object_to_json_string(j);
        response = MHD_create_response_from_buffer(strlen(s), (void*)s, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json; charset=UTF-8");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        json_object_put(j);
        return ret;
    }

    if (0 == strcmp(method, "GET") && url && strcmp(url, "/api/v1/metrics") == 0) {
        pthread_mutex_lock(&storage_mutex);
        json_object *metrics = json_object_from_file(METRICS_FILE);
        if (!metrics) {
            metrics = json_object_new_object();
            json_object_object_add(metrics, "message", json_object_new_string("no metrics yet"));
        }
        const char *s2 = json_object_to_json_string(metrics);
        response = MHD_create_response_from_buffer(strlen(s2), (void*)s2, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json; charset=UTF-8");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        json_object_put(metrics);
        pthread_mutex_unlock(&storage_mutex);
        return ret;
    }

    if (0 == strcmp(method, "GET") && url && strcmp(url, "/api/v1/rules") == 0) {
        pthread_mutex_lock(&storage_mutex);
        json_object *list = rule_engine_list(&rule_engine);
        const char *s4 = json_object_to_json_string(list);
        response = MHD_create_response_from_buffer(strlen(s4), (void*)s4, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json; charset=UTF-8");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        json_object_put(list);
        pthread_mutex_unlock(&storage_mutex);
        return ret;
    }

    if (0 == strcmp(method, "POST") && url && strcmp(url, "/api/v1/sync_rules") == 0) {
        // trigger a file-based sync from cluster_rules.json
        pthread_mutex_lock(&storage_mutex);
        int added = rule_engine_sync(&rule_engine, "cluster_rules.json");
        pthread_mutex_unlock(&storage_mutex);
        char msg[128];
        snprintf(msg, sizeof(msg), "synced %d rules", added);
        response = MHD_create_response_from_buffer(strlen(msg), (void*)msg, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/plain; charset=UTF-8");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    if (0 == strcmp(method, "POST") && url && strcmp(url, "/api/v1/rules") == 0) {
        // body will be processed below; just mark the URL here
    }

    // GET /api/v1/tasks?limit=N -> return last N entries
    if (0 == strcmp(method, "GET") && url && strncmp(url, "/api/v1/tasks", 12) == 0) {
        pthread_mutex_lock(&storage_mutex);
        json_object *root = json_object_from_file(LEARNING_FILE);
        if (!root) {
            root = json_object_new_array();
        }
        // parse limit
        int limit = 10;
        const char *q = strchr(url, '?');
        if (q) {
            char *qcopy = strdup(q + 1);
            char *tok = strtok(qcopy, "&");
            while (tok) {
                if (strncmp(tok, "limit=", 6) == 0) limit = atoi(tok + 6);
                tok = strtok(NULL, "&");
            }
            free(qcopy);
        }
        size_t n = json_object_array_length(root);
        json_object *out = json_object_new_array();
        for (int i = (int) (n - (size_t)limit < 0 ? 0 : n - limit); i < (int)n; ++i) {
            json_object_array_add(out, json_object_get(json_object_array_get_idx(root, i)));
        }
        const char *s3 = json_object_to_json_string(out);
        response = MHD_create_response_from_buffer(strlen(s3), (void*)s3, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json; charset=UTF-8");
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        json_object_put(out);
        json_object_put(root);
        pthread_mutex_unlock(&storage_mutex);
        return ret;
    }

    if (0 == strcmp(method, "POST")) {
        /* Приём тела POST */
        if (*upload_data_size != 0) {
            size_t to_copy = *upload_data_size;
            if (post_ctx->pos + to_copy < MAX_BUFFER) {
                memcpy(post_ctx->buffer + post_ctx->pos, upload_data, to_copy);
                post_ctx->pos += to_copy;
                post_ctx->buffer[post_ctx->pos] = '\0';
            } else {
                /* усечём входные данные при превышении буфера */
                size_t can = MAX_BUFFER - 1 - post_ctx->pos;
                if (can > 0) {
                    memcpy(post_ctx->buffer + post_ctx->pos, upload_data, can);
                    post_ctx->pos += can;
                    post_ctx->buffer[post_ctx->pos] = '\0';
                }
            }
            *upload_data_size = 0;
            return MHD_YES;
        }

        if (post_ctx->processed) return MHD_YES;
        post_ctx->processed = 1;

        const char* content_type = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Content-Type");
        if (!content_type) {
            const char msg[] = "Invalid Content-Type";
            response = MHD_create_response_from_buffer(strlen(msg), (void*)msg, MHD_RESPMEM_PERSISTENT);
            ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
            MHD_destroy_response(response);
            return ret;
        }

        int is_json = strstr(content_type, "application/json") != NULL;
        int is_form = strstr(content_type, "application/x-www-form-urlencoded") != NULL;

        // JSON API: /api/v1/solve
        if (is_json && url && strcmp(url, "/api/v1/solve") == 0) {
            json_object *jroot = json_tokener_parse(post_ctx->buffer);
            if (!jroot) {
                const char msg[] = "{\"error\":\"invalid json\"}";
                response = MHD_create_response_from_buffer(strlen(msg), (void*)msg, MHD_RESPMEM_PERSISTENT);
                MHD_add_response_header(response, "Content-Type", "application/json; charset=UTF-8");
                ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
                MHD_destroy_response(response);
                return ret;
            }
            json_object *jtask = NULL;
            if (!json_object_object_get_ex(jroot, "task", &jtask)) {
                const char msg[] = "{\"error\":\"missing task\"}";
                response = MHD_create_response_from_buffer(strlen(msg), (void*)msg, MHD_RESPMEM_PERSISTENT);
                MHD_add_response_header(response, "Content-Type", "application/json; charset=UTF-8");
                ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
                MHD_destroy_response(response);
                json_object_put(jroot);
                return ret;
            }
            const char *taskv = json_object_get_string(jtask);
            if (!taskv) taskv = "";
            const char *result = process_task(taskv);

            // Read last saved learning entry to extract metrics
            pthread_mutex_lock(&storage_mutex);
            json_object *root = json_object_from_file(LEARNING_FILE);
            json_object *last = NULL;
            if (root && json_object_array_length(root) > 0) {
                last = json_object_array_get_idx(root, json_object_array_length(root) - 1);
                if (last) json_object_get(last); // bump ref
            }
            json_object *out = json_object_new_object();
            json_object_object_add(out, "task", json_object_new_string(taskv));
            json_object_object_add(out, "result", json_object_new_string(result ? result : ""));
            if (last) {
                json_object *jeff = NULL, *jcomp = NULL, *jtime = NULL;
                if (json_object_object_get_ex(last, "effectiveness", &jeff)) json_object_object_add(out, "effectiveness", json_object_new_double(json_object_get_double(jeff)));
                if (json_object_object_get_ex(last, "complexity", &jcomp)) json_object_object_add(out, "complexity", json_object_new_double(json_object_get_double(jcomp)));
                if (json_object_object_get_ex(last, "timestamp", &jtime)) json_object_object_add(out, "timestamp", json_object_new_int64(json_object_get_int64(jtime)));
                            json_object *jused = NULL;
                            if (json_object_object_get_ex(last, "used_rule_id", &jused)) json_object_object_add(out, "used_rule_id", json_object_new_string(json_object_get_string(jused)));
                json_object_put(last);
            }
            if (root) json_object_put(root);
            const char *s = json_object_to_json_string(out);
            response = MHD_create_response_from_buffer(strlen(s), (void*)s, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(response, "Content-Type", "application/json; charset=UTF-8");
            ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);
            json_object_put(out);
            pthread_mutex_unlock(&storage_mutex);
            json_object_put(jroot);
            return ret;
        }

        if (!post_ctx->decoded_task) {
            post_ctx->decoded_task = malloc(MAX_BUFFER);
            if (!post_ctx->decoded_task) return MHD_NO;
            post_ctx->decoded_task[0] = '\0';
        }

        char response_str[MAX_BUFFER];
        response_str[0] = '\0';

        if (is_json) {
            json_object *jroot = json_tokener_parse(post_ctx->buffer);
            if (!jroot) {
                snprintf(response_str, sizeof(response_str), "Ошибка: неверный JSON");
            } else {
                json_object *jtask = NULL, *jfeedback = NULL;
                // If posting a rule: POST /api/v1/rules
                json_object *jexpr = NULL;
                json_object_object_get_ex(jroot, "expr", &jexpr);
                if ((url && strcmp(url, "/api/v1/rules") == 0) || jexpr) {
                    pthread_mutex_lock(&storage_mutex);
                    int r = rule_engine_add(&rule_engine, jroot);
                    pthread_mutex_unlock(&storage_mutex);
                    if (r == 0) {
                        snprintf(response_str, sizeof(response_str), "Rule accepted");
                    } else {
                        snprintf(response_str, sizeof(response_str), "Failed to save rule");
                    }
                } else if (json_object_object_get_ex(jroot, "task", &jtask)) {
                    const char *taskv = json_object_get_string(jtask);
                    if (taskv) {
                        strncpy(post_ctx->decoded_task, taskv, MAX_BUFFER - 1);
                        post_ctx->decoded_task[MAX_BUFFER - 1] = '\0';
                        // detect explicit save format: 'запомни: key=val'
                        if (strstr(post_ctx->decoded_task, "запомни:") && strchr(post_ctx->decoded_task, '=')) {
                            // extract substring after 'запомни:'
                            const char *p = strstr(post_ctx->decoded_task, "запомни:") + strlen("запомни:");
                            while (*p == ' ') p++;
                            // create rule json
                            json_object *r = json_object_new_object();
                            json_object_object_add(r, "expr", json_object_new_string(p));
                            json_object_object_add(r, "origin", json_object_new_string("local"));
                            rule_engine_add(&rule_engine, r);
                            json_object_put(r);
                            const char *result = "Правило сохранено";
                            strncpy(post_ctx->decoded_task, post_ctx->decoded_task, MAX_BUFFER - 1);
                            snprintf(response_str, sizeof(response_str), "%s", result);
                            log_task(post_ctx->decoded_task, result);
                        } else {
                            const char* result = process_task(post_ctx->decoded_task);
                            snprintf(response_str, sizeof(response_str), "%s", result);
                            log_task(post_ctx->decoded_task, result);
                        }
                    }
                } else if (json_object_object_get_ex(jroot, "feedback", &jfeedback)) {
                    int rating = json_object_get_int(jfeedback);
                    if (rating < 1) rating = 1; if (rating > 5) rating = 5;
                    json_object* root = json_object_from_file(LEARNING_FILE);
                    if (root && json_object_array_length(root) > 0) {
                        json_object* last_entry = json_object_array_get_idx(root, json_object_array_length(root) - 1);
                        json_object_object_add(last_entry, "rating", json_object_new_int(rating));
                        json_object_to_file(LEARNING_FILE, root);
                    }
                    if (root) json_object_put(root);

                    // If last entry used a rule, and rating is good, record success
                    json_object* root_check = json_object_from_file(LEARNING_FILE);
                    if (root_check && json_object_array_length(root_check) > 0) {
                        json_object* last_entry = json_object_array_get_idx(root_check, json_object_array_length(root_check) - 1);
                        json_object *jused = NULL;
                        if (json_object_object_get_ex(last_entry, "used_rule_id", &jused)) {
                            const char *rid = json_object_get_string(jused);
                            if (rid && rating >= 4) {
                                pthread_mutex_lock(&storage_mutex);
                                rule_engine_record_success(&rule_engine, rid);
                                pthread_mutex_unlock(&storage_mutex);
                            }
                        }
                    }
                    if (root_check) json_object_put(root_check);
                    snprintf(response_str, sizeof(response_str), "Спасибо за оценку: %d звезд! Ваш отзыв поможет улучшить будущие ответы.", rating);

                    /* обновляем модель по последней записи */
                    json_object* root2 = json_object_from_file(LEARNING_FILE);
                    if (root2 && json_object_array_length(root2) > 0) {
                        json_object* last_entry = json_object_array_get_idx(root2, json_object_array_length(root2) - 1);
                        json_object *j_task = NULL;
                        if (json_object_object_get_ex(last_entry, "task", &j_task)) {
                            const char* last_task = json_object_get_string(j_task);
                            const size_t EMB_DIM = 64;
                            double emb[EMB_DIM];
                            embed_text(last_task, emb, EMB_DIM);
                            double mean = 0.0, mx = -1e9;
                            for (size_t i = 0; i < EMB_DIM; ++i) { mean += emb[i]; if (emb[i] > mx) mx = emb[i]; }
                            mean /= (double)EMB_DIM;
                            double len_norm = (double)strlen(last_task) / (double)MAX_BUFFER;
                            double features_small[3] = { mean < 0.0 ? 0.0 : mean, mx < 0.0 ? 0.0 : mx, len_norm };
                            for (int fi = 0; fi < 3; ++fi) { if (features_small[fi] < 0.0) features_small[fi] = 0.0; if (features_small[fi] > 1.0) features_small[fi] = 1.0; }
                            double target = (double)rating / 5.0;
                            mlp_update(features_small, target, 0.01);
                            if (mlp_save("mlp_weights.bin") != 0) printf("Warning: mlp_save failed\n");
                        }
                    }
                    if (root2) json_object_put(root2);
                } else {
                    snprintf(response_str, sizeof(response_str), "Ошибка: неизвестный тип запроса");
                }
                json_object_put(jroot);
            }
        } else if (is_form) {
            char *buf = post_ctx->buffer;
            char *saveptr = NULL;
            char *pair = NULL;
            int handled = 0;
            for (pair = strtok_r(buf, "&", &saveptr); pair; pair = strtok_r(NULL, "&", &saveptr)) {
                char *eq = strchr(pair, '=');
                if (!eq) continue;
                *eq = '\0';
                const char *k = pair;
                const char *v = eq + 1;
                char decoded_val[MAX_BUFFER];
                url_decode(v, decoded_val, sizeof(decoded_val));

                if (strcmp(k, "task") == 0) {
                    strncpy(post_ctx->decoded_task, decoded_val, MAX_BUFFER - 1);
                    post_ctx->decoded_task[MAX_BUFFER - 1] = '\0';
                    if (strlen(post_ctx->decoded_task) == 0) {
                        snprintf(response_str, sizeof(response_str), "Ошибка: пустая задача");
                    } else {
                        // detect explicit save format: 'запомни: key=val'
                        if (strstr(post_ctx->decoded_task, "запомни:") && strchr(post_ctx->decoded_task, '=')) {
                            const char *p = strstr(post_ctx->decoded_task, "запомни:") + strlen("запомни:");
                            while (*p == ' ') p++;
                            json_object *r = json_object_new_object();
                            json_object_object_add(r, "expr", json_object_new_string(p));
                            json_object_object_add(r, "origin", json_object_new_string("local"));
                            rule_engine_add(&rule_engine, r);
                            json_object_put(r);
                            snprintf(response_str, sizeof(response_str), "Правило сохранено");
                            log_task(post_ctx->decoded_task, response_str);
                        } else {
                            const char* result = process_task(post_ctx->decoded_task);
                            snprintf(response_str, sizeof(response_str), "%s", result);
                            log_task(post_ctx->decoded_task, result);
                        }
                    }
                    handled = 1;
                } else if (strcmp(k, "feedback") == 0 || strcmp(k, "rating") == 0) {
                    int rating = atoi(decoded_val);
                    if (rating < 1) rating = 1;
                    if (rating > 5) rating = 5;
                    json_object* root = json_object_from_file(LEARNING_FILE);
                    if (root && json_object_array_length(root) > 0) {
                        json_object* last_entry = json_object_array_get_idx(root, json_object_array_length(root) - 1);
                        json_object_object_add(last_entry, "rating", json_object_new_int(rating));
                        json_object_to_file(LEARNING_FILE, root);
                    }
                    if (root) json_object_put(root);
                    snprintf(response_str, sizeof(response_str), "Спасибо за оценку: %d звезд! Ваш отзыв поможет улучшить будущие ответы.", rating);

                    json_object* root2 = json_object_from_file(LEARNING_FILE);
                    if (root2 && json_object_array_length(root2) > 0) {
                        json_object* last_entry = json_object_array_get_idx(root2, json_object_array_length(root2) - 1);
                        json_object *j_task = NULL;
                        if (json_object_object_get_ex(last_entry, "task", &j_task)) {
                            const char* last_task = json_object_get_string(j_task);
                            const size_t EMB_DIM = 64;
                            double emb[EMB_DIM];
                            embed_text(last_task, emb, EMB_DIM);
                            double mean = 0.0, mx = -1e9;
                            for (size_t i = 0; i < EMB_DIM; ++i) { mean += emb[i]; if (emb[i] > mx) mx = emb[i]; }
                            mean /= (double)EMB_DIM;
                            double len_norm = (double)strlen(last_task) / (double)MAX_BUFFER;
                            double features_small[3] = { mean < 0.0 ? 0.0 : mean, mx < 0.0 ? 0.0 : mx, len_norm };
                            for (int fi = 0; fi < 3; ++fi) { if (features_small[fi] < 0.0) features_small[fi] = 0.0; if (features_small[fi] > 1.0) features_small[fi] = 1.0; }
                            double target = (double)rating / 5.0;
                            mlp_update(features_small, target, 0.01);
                            if (mlp_save("mlp_weights.bin") != 0) printf("Warning: mlp_save failed\n");
                        }
                    }
                    if (root2) json_object_put(root2);
                    handled = 1;
                }
            }
            if (!handled) snprintf(response_str, sizeof(response_str), "Ошибка: неизвестный тип запроса");
        } else {
            snprintf(response_str, sizeof(response_str), "Invalid Content-Type: %s", content_type);
        }

        response = MHD_create_response_from_buffer(strlen(response_str), (void*)response_str, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "text/plain; charset=UTF-8");
    } else {
        /* GET - отдаём HTML */
        response = MHD_create_response_from_buffer(strlen(html_page), (void*)html_page, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Content-Type", "text/html; charset=UTF-8");
    }

    MHD_add_response_header(response, "Cache-Control", "no-cache");
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);

    /* очищаем контекст */
    if (*con_cls) {
        struct PostContext *ctx = *con_cls;
        if (ctx->decoded_task) free(ctx->decoded_task);
        free(ctx);
        *con_cls = NULL;
    }
    return ret;
}

int main(int argc, char **argv) {
    struct MHD_Daemon *daemon;
    int port = PORT;
    if (argc > 1) {
        int p = atoi(argv[1]);
        if (p > 0 && p < 65536) port = p;
    }

    // Инициализация эмбеддингов и MLP
    const size_t EMB_DIM = 64;
    const size_t HIDDEN = 32;
    if (embed_init(EMB_DIM) != 0) {
        fprintf(stderr, "Failed to init embed\n");
        // продолжаем, это не критично
    }
    // MLP принимает 3 входа (агрегированные фичи)
    if (mlp_init(3, HIDDEN, 1) != 0) {
        fprintf(stderr, "Failed to init mlp\n");
    } else {
        // Попробуем загрузить веса, если файл есть
        mlp_load("mlp_weights.bin");
    }

    server_start_time = time(NULL);

    // Initialize node brain storage prefix based on port
    char storage_prefix[256];
    snprintf(storage_prefix, sizeof(storage_prefix), "node_%d", port);
    if (node_brain_init(&node_brain, storage_prefix) != 0) {
        log_server_event("Warning: node_brain_init failed");
    }
    if (rule_engine_init(&rule_engine, storage_prefix) != 0) {
        log_server_event("Warning: rule_engine_init failed");
    }

    // start rule GC worker
    rule_gc_stop = 0;
    if (pthread_create(&rule_gc_thread, NULL, rule_gc_worker, &rule_engine) != 0) {
        log_server_event("Warning: failed to start rule_gc_thread");
    }

    daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_DEBUG | MHD_USE_ERROR_LOG,
        port,
        NULL,
        NULL,
        &request_handler,
        NULL,
        MHD_OPTION_CONNECTION_TIMEOUT, 120,
        MHD_OPTION_END);

    if (NULL == daemon) {
        fprintf(stderr, "Failed to start server on port %d\n", port);
        // Очистим ресурсы
        mlp_free();
        embed_free();
        return 1;
    }

    printf("Server is running on http://localhost:%d\n", port);

    // Установим обработчики сигналов для корректного завершения
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sig;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Ждём сигнала завершения (без привязки к TTY)
    while (!server_stop) {
        sleep(1);
    }

    // Сохраняем веса и освобождаем ресурсы
    if (mlp_save("mlp_weights.bin") != 0) fprintf(stderr, "Warning: failed to save mlp weights\n");
    mlp_free();
    embed_free();

    // save node brain state
    if (node_brain_save(&node_brain, storage_prefix) != 0) {
        log_server_event("Warning: failed to save node brain state");
    }
    node_brain_free(&node_brain);
    if (rule_engine_save(&rule_engine) != 0) log_server_event("Warning: failed to save rules");
    rule_engine_free(&rule_engine);

    // stop GC worker
    rule_gc_stop = 1;
    pthread_join(rule_gc_thread, NULL);

    MHD_stop_daemon(daemon);
    return 0;
}
