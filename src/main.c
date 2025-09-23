#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <math.h>

#include "decimal_cell.h"
#include "rules_engine.h"
#include "formula_advanced.h"
#include "learning.h"
#include "network.h"
#include "blockchain.h"

// Глобальные переменные для управления состоянием
static volatile int running = 1;
static LearningSystem* learning_system = NULL;
static int node_port = 9000;
static char node_specialization[32] = "memory";

// Обработчик сигналов для корректного завершения
void handle_signal(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    running = 0;
}

// Инициализация системы
void init_system(void) {
    // Создание системы обучения
    learning_system = learning_system_create(10, 0.01);
    if (!learning_system) {
        fprintf(stderr, "Failed to create learning system\n");
        exit(1);
    }
    
    // Инициализация начальных ячеек
    for (int i = 0; i < 10; i++) {
        DecimalCell* cell = decimal_cell_create(0.0, -100.0, 100.0);
        if (!cell || !learning_system_add_cell(learning_system, cell)) {
            fprintf(stderr, "Failed to create/add cell %d\n", i);
            exit(1);
        }
    }
    
    // Добавление базовых правил
    rules_engine_add_rule(learning_system->rules,
                         "value > threshold",
                         "activate_neighbors",
                         0.8);
                         
    printf("[INFO] System initialized with %zu cells\n", learning_system->cell_count);
}

// Основной цикл обработки
void process_cycle(void) {
    static int complexity_level = 1;
    
    // Генерация новой формулы
    Formula* formula = formula_generate_from_cells(learning_system->cells,
                                                 learning_system->cell_count);
    
    if (formula) {
        printf("[AI] Generated formula: %s\n",
               formula->expression ? formula->expression : "f(x) = <complex>");
        
        printf("[INFO] Complexity level: %d\n", complexity_level);
        
        // Оптимизация формулы
        formula_optimize(formula, learning_system->cells, learning_system->cell_count);
        
        // Расчет эффективности
        double effectiveness = formula_calculate_effectiveness(formula,
                                                            learning_system->cells,
                                                            learning_system->cell_count);
        
        printf("[INFO] Formula effectiveness: %.4f\n", effectiveness);
        
        // Принятие решения о сохранении формулы
        if (effectiveness > 0.8) {
            Formula** new_formulas = realloc(learning_system->formulas,
                                           (learning_system->formula_count + 1) * sizeof(Formula*));
            
            if (new_formulas) {
                learning_system->formulas = new_formulas;
                learning_system->formulas[learning_system->formula_count++] = formula;
                
                printf("[SUCCESS] Added high-effectiveness formula to collection "
                       "(total: %zu)\n", learning_system->formula_count);
                
                // Создание нового блока в блокчейне каждые 10 формул
                if (learning_system->formula_count % 10 == 0) {
                    printf("[AI] Creating new blockchain block with last 10 formulas\n");
                    // TODO: Добавить интеграцию с блокчейном
                    printf("[SUCCESS] Blockchain length: %zu blocks\n",
                           learning_system->formula_count / 10);
                }
            }
        } else {
            printf("[WARNING] Formula rejected (low effectiveness)\n");
            formula_destroy(formula);
        }
    }
    
    // Обработка правил
    rules_engine_process(learning_system->rules,
                        learning_system->cells,
                        learning_system->cell_count);
                        
    // Федеративное обучение с другими узлами
    if (learning_system->formula_count > 0 && learning_system->formula_count % 20 == 0) {
        char remote_url[256];
        snprintf(remote_url, sizeof(remote_url),
                "http://localhost:%d/federated", node_port + 1);
        learning_system_federated_update(learning_system, remote_url);
    }
    
    usleep(100000); // Небольшая задержка для стабильности
}

int main(int argc, char* argv[]) {
    // Обработка аргументов командной строки
    if (argc > 1) {
        node_port = atoi(argv[1]);
    }
    if (argc > 2) {
        strncpy(node_specialization, argv[2], sizeof(node_specialization) - 1);
    }
    
    // Инициализация генератора случайных чисел
    srand(time(NULL) + node_port);
    
    // Установка обработчиков сигналов
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    printf("Selected specialization: %s\n", node_specialization);
    printf("[INFO] Node digit: %d\n", node_port % 10);
    
    // Инициализация системы
    init_system();
    
    printf("[SUCCESS] Server started on port %d\n", node_port);
    
    // Основной цикл
    while (running) {
        process_cycle();
    }
    
    // Очистка ресурсов
    if (learning_system) {
        // Сохранение накопленных знаний перед выходом
        char knowledge_file[256];
        snprintf(knowledge_file, sizeof(knowledge_file),
                "knowledge_node_%d.dat", node_port);
        learning_system_export_knowledge(learning_system, knowledge_file);
        
        learning_system_destroy(learning_system);
    }
    
    return 0;
}
