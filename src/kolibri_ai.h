#ifndef KOLIBRI_AI_H
#define KOLIBRI_AI_H

#include "formula.h"
#include "kovian_blockchain.h"
#include <pthread.h>

// Внешние функции
int get_formula_type(const char* content);

// Состояние AI-подсистемы узла
typedef struct {
    FormulaCollection* formulas;     // Локальная коллекция формул
    KovianChain* blockchain;         // Локальный блокчейн
    pthread_mutex_t mutex;           // Мьютекс для синхронизации
    pthread_t ai_thread;             // Поток AI-обработки
    int running;                     // Флаг работы
    
    // Параметры самообучения
    int complexity_level;            // Текущий уровень сложности формул
    double learning_rate;            // Скорость обучения
    unsigned long iterations;        // Счетчик итераций
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
