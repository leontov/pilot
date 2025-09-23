#include "kolibri_ai.h"
#include "formula.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <uuid/uuid.h>

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
    
    if (!ai->formulas || !ai->blockchain) {
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
    pthread_mutex_lock(&ai->mutex);
    
    // Генерируем новую формулу
    Formula* formula = generate_random_formula(ai->complexity_level);
    if (formula) {
        LOG_AI("Generated formula: %s", formula->content);
        LOG_INFO("Complexity level: %d", ai->complexity_level);
        
        // Проверяем и оцениваем
        if (validate_formula(formula)) {
            formula->effectiveness = evaluate_effectiveness(formula);
            LOG_INFO("Formula effectiveness: %.4f", formula->effectiveness);
            
            // Если формула достаточно эффективна
            if (formula->effectiveness >= 0.7) {
                formula_collection_add(ai->formulas, formula);
                LOG_SUCCESS("Added high-effectiveness formula to collection (total: %zu)", 
                          ai->formulas->count);
                
                // Создаем новый блок каждые 10 формул
                if (ai->formulas->count % 10 == 0) {
                    LOG_AI("Creating new blockchain block with last 10 formulas");
                    kovian_chain_add_block(ai->blockchain, 
                                         &ai->formulas->formulas[ai->formulas->count - 10],
                                         10);
                    LOG_SUCCESS("Blockchain length: %zu blocks", ai->blockchain->length);
                }
            } else {
                LOG_WARNING("Formula rejected (low effectiveness)");
            }
        } else {
            LOG_ERROR("Formula validation failed");
        }
        formula_clear(formula);
        free(formula);
    }
    
    // Адаптируем параметры
    ai->iterations++;
    
    // Каждые 100 итераций
    if (ai->iterations % 100 == 0) {
        // Делимся лучшей формулой с соседями
        Formula* best = kolibri_ai_get_best_formula(ai);
        if (best && best->effectiveness >= 0.6) {  // Порог для обмена
            char* json = serialize_formula(best);
            if (json) {
                printf("[AI] Sharing best formula: %s (eff=%.4f)\n",
                       best->content, best->effectiveness);
                // TODO: Отправить json соседям
                free(json);
            }
            formula_clear(best);
            free(best);
        }
    }
    
    // Каждые 500 итераций - комбинируем формулы
    if (ai->iterations % 500 == 0 && ai->formulas->count >= 2) {
        // Берем две лучшие формулы
        Formula* best1 = &ai->formulas->formulas[0];
        Formula* best2 = &ai->formulas->formulas[1];
        
        // Создаем новую композитную формулу
        Formula* combined = calloc(1, sizeof(Formula));
        if (combined) {
            uuid_t uuid;
            uuid_generate(uuid);
            uuid_unparse(uuid, combined->id);
            
            // Комбинируем формулы разными способами
            if (rand() % 2 == 0) {
                snprintf(combined->content, sizeof(combined->content),
                        "f(x) = (%s) + (%s)", 
                        best1->content + 7,  // Пропускаем "f(x) = "
                        best2->content + 7);
            } else {
                snprintf(combined->content, sizeof(combined->content),
                        "f(x) = (%s) * (%s)", 
                        best1->content + 7,
                        best2->content + 7);
            }
            
            combined->effectiveness = 0.0;
            combined->created_at = time(NULL);
            combined->tests_passed = 0;
            combined->confirmations = 0;
            combined->representation = FORMULA_REPRESENTATION_TEXT;
            combined->coefficients = NULL;
            combined->coeff_count = 0;
            combined->expression = NULL;
            combined->type = FORMULA_LINEAR;

            // Оцениваем и возможно добавляем
            if (validate_formula(combined)) {
                combined->effectiveness = evaluate_effectiveness(combined);
                if (combined->effectiveness >= 0.35) {
                    formula_collection_add(ai->formulas, combined);
                    printf("[AI] Created combined formula: %s (eff=%.4f)\n",
                           combined->content, combined->effectiveness);
                }
            }
            formula_clear(combined);
            free(combined);
        }
    }

    // Каждые 1000 итераций - адаптируем параметры
    if (ai->iterations % 1000 == 0) {
        // Увеличиваем сложность если есть успешные формулы
        if (ai->formulas->count > 0) {
            ai->complexity_level++;
        }
        // Корректируем сложность блокчейна
        adjust_chain_difficulty(ai->blockchain);
        
        // Выводим статистику
        printf("[AI] Status: complexity=%d, formulas=%zu, best_eff=%.4f\n",
               ai->complexity_level, ai->formulas->count,
               ai->formulas->count > 0 ? ai->formulas->formulas[0].effectiveness : 0.0);
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
    
    Formula* formula = deserialize_formula(json);
    if (!formula) return -1;
    
    int result = -1;
    
    // Проверяем формулу
    if (formula->representation == FORMULA_REPRESENTATION_TEXT && validate_formula(formula)) {
        // Переоцениваем эффективность локально
        formula->effectiveness = evaluate_effectiveness(formula);
        
        // Увеличиваем счетчик подтверждений
        formula->confirmations++;
        
        // Проверяем эффективность с учетом предыдущих подтверждений
        double adjusted_effectiveness = 
            formula->effectiveness * (1.0 + log(formula->confirmations) / 10.0);
        
        // Бонус за разнообразие типов формул
        int type = get_formula_type(formula->content);
        if (type > 0) {  // Не полиномиальная
            adjusted_effectiveness *= 1.2;  // 20% бонус за сложность
        }
        
        // Снижаем порог для принятия формул
        if (adjusted_effectiveness >= 0.35) {  // Порог снижен с 0.4 до 0.35
            result = kolibri_ai_add_formula(ai, formula);
            
            // Если формула очень хорошая, делимся ей с соседями
            if (adjusted_effectiveness >= 0.7) {
                // TODO: Реализовать broadcast соседям
                printf("[AI] Broadcasting high-quality formula: %s (eff=%.4f)\n",
                       formula->content, adjusted_effectiveness);
            }
        }
    }
    
    formula_clear(formula);
    free(formula);
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
