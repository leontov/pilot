#ifndef KOLIBRI_AI_H
#define KOLIBRI_AI_H

#include "formula.h"
#include "kovian_blockchain.h"
#include <pthread.h>
#include <time.h>
#include <stddef.h>

// Внешние функции
int get_formula_type(const char* content);

// Типы долгосрочной памяти
typedef enum {
    KOLIBRI_MEMORY_FACT = 0,
    KOLIBRI_MEMORY_EPISODE = 1
} KolibriMemoryEntryType;

typedef struct {
    KolibriMemoryEntryType type;     // Тип памяти (факт/эпизод)
    char id[64];                     // Уникальный идентификатор записи
    char description[256];           // Описание факта или эпизода
    char source[128];                // Источник наблюдения
    double importance;               // Важность/релевантность
    double reward;                   // Последняя награда
    time_t timestamp;                // Временная метка
} KolibriMemoryEntry;

typedef struct {
    KolibriMemoryEntry* entries;
    size_t count;
    size_t capacity;
} KolibriMemoryModule;

// Сенсорное состояние узла
typedef struct {
    char modality[64];               // Тип сенсора или канала данных
    double value;                    // Последнее значение
    double confidence;               // Уверенность модели
    time_t timestamp;                // Время обновления
} KolibriSensorReading;

typedef struct {
    KolibriSensorReading* readings;
    size_t count;
    size_t capacity;
} KolibriSensorState;

// Структура параметризованной цели
typedef struct {
    char id[64];
    char description[128];
    double priority;                 // Вес цели в планировщике
    double target_value;             // Целевое значение метрики
    double tolerance;                // Допустимое отклонение
} KolibriGoal;

typedef struct {
    KolibriGoal* items;
    size_t count;
    size_t capacity;
} KolibriGoalSet;

// Обучаемые модели
typedef struct {
    double* parameters;              // Веса модели
    size_t parameter_count;
    double learning_rate;            // Скорость обучения
} MLPModel;

typedef struct {
    double* parameters;              // Веса трансформера
    size_t parameter_count;
    size_t head_count;               // Количество голов внимания
    double dropout;                  // Используемый дропаут
} TransformerModel;

// Состояние AI-подсистемы узла
typedef struct {
    FormulaCollection* formulas;         // Локальная коллекция формул
    KovianChain* blockchain;             // Локальный блокчейн
    pthread_mutex_t mutex;               // Мьютекс для синхронизации
    pthread_t ai_thread;                 // Поток AI-обработки
    int running;                         // Флаг работы

    KolibriMemoryModule* memory;         // Модуль долгосрочной памяти
    KolibriSensorState sensor_state;     // Актуальное сенсорное состояние
    KolibriGoalSet goals;                // Набор целей узла

    MLPModel* policy_mlp;                // Нейросеть планировщика (MLP)
    TransformerModel* world_model;       // Модель мирового состояния
    FormulaTrainingPipeline* pipeline;   // Пайплайн обучения формул

    FormulaExperience last_experience;   // Последняя полученная обратная связь
    double last_plan_score;              // Последний итоговый скор планировщика

    // Параметры самообучения
    int complexity_level;                // Текущий уровень сложности формул
    double learning_rate;                // Скорость обучения
    unsigned long iterations;            // Счетчик итераций
} KolibriAI;

// Функции инициализации и управления
KolibriAI* kolibri_ai_create(void);
void kolibri_ai_destroy(KolibriAI* ai);
void kolibri_ai_start(KolibriAI* ai);
void kolibri_ai_stop(KolibriAI* ai);

// Функции самообучения
void kolibri_ai_process_iteration(KolibriAI* ai);
int kolibri_ai_add_formula(KolibriAI* ai, const Formula* formula);
Formula* kolibri_ai_get_best_formula(KolibriAI* ai);

// Сетевые функции
char* kolibri_ai_serialize_state(const KolibriAI* ai);
int kolibri_ai_process_remote_formula(KolibriAI* ai, const char* json);
void kolibri_ai_sync_with_neighbor(KolibriAI* ai, const char* neighbor_url);

#endif // KOLIBRI_AI_H
