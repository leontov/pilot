#ifndef KOLIBRI_DECIMAL_CELL_H
#define KOLIBRI_DECIMAL_CELL_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_NEIGHBORS 9  // максимум 9 соседей (цифры 0-9, кроме собственной)
#define SYNC_INTERVAL 1000  // интервал синхронизации в мс

typedef struct {
    uint8_t node_digit;          // собственная цифра узла (0-9)
    uint8_t neighbor_digits[9];  // цифры соседей
    uint8_t n_neighbors;         // текущее количество соседей
    uint64_t last_sync[9];      // время последней синхронизации с каждым соседом
    bool is_active[9];          // активность соседей
} decimal_cell_t;

// Инициализация узла с его цифрой
void init_decimal_cell(decimal_cell_t* cell, uint8_t digit);

// Очистка ресурсов узла
void cleanup_decimal_cell(decimal_cell_t* cell);

// Добавление соседа
int add_neighbor(decimal_cell_t* cell, uint8_t digit);

// Удаление соседа
void remove_neighbor(decimal_cell_t* cell, uint8_t digit);

// Проверка необходимости синхронизации с соседом
bool needs_sync(decimal_cell_t* cell, uint8_t neighbor_idx);

// Маркировка успешной синхронизации
void mark_sync(decimal_cell_t* cell, uint8_t neighbor_idx);

// Проверка активности соседа
bool is_neighbor_active(decimal_cell_t* cell, uint8_t neighbor_idx);

// Получение индекса соседа по его цифре
int get_neighbor_index(decimal_cell_t* cell, uint8_t digit);

// Обновление состояния ячейки
void update_cell_state(decimal_cell_t* cell);

#endif
