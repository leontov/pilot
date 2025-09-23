#ifndef KOLIBRI_DECIMAL_CELL_H
#define KOLIBRI_DECIMAL_CELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DECIMAL_CELL_FANOUT 10
#define SYNC_INTERVAL 1000  // интервал синхронизации в мс

typedef struct decimal_cell_t {
    uint8_t digit;                              // цифра узла на текущем уровне
    uint8_t depth;                              // глубина в дереве (0 — корень)
    bool is_active;                             // активен ли узел
    uint64_t created_at;                        // время создания
    uint64_t last_state_change;                 // последнее изменение активности
    uint64_t last_sync_time;                    // последняя синхронизация узла
    uint64_t sync_interval;                     // требуемый интервал синхронизации
    struct decimal_cell_t* parent;              // родительский узел
    struct decimal_cell_t* children[DECIMAL_CELL_FANOUT];
    bool child_active[DECIMAL_CELL_FANOUT];     // активность дочерних узлов
    uint64_t child_last_sync[DECIMAL_CELL_FANOUT];
    uint64_t child_last_state_change[DECIMAL_CELL_FANOUT];
} decimal_cell_t;

// Инициализация корня десятичного дерева.
void init_decimal_cell(decimal_cell_t* cell, uint8_t digit);

// Полная очистка дерева (рекурсивное освобождение всех дочерних узлов).
void cleanup_decimal_cell(decimal_cell_t* cell);

// Добавление или активация ветки по массиву индексов.
decimal_cell_t* decimal_cell_add_path(decimal_cell_t* root,
                                      const uint8_t* path,
                                      size_t path_len,
                                      bool activate);

// Аналог decimal_cell_add_path, но путь задаётся строкой цифр.
decimal_cell_t* decimal_cell_add_path_str(decimal_cell_t* root,
                                          const char* path,
                                          bool activate);

// Поиск узла по массиву индексов без модификации дерева.
decimal_cell_t* decimal_cell_find_path(decimal_cell_t* root,
                                       const uint8_t* path,
                                       size_t path_len);

// Поиск узла по строке цифр.
decimal_cell_t* decimal_cell_find_path_str(decimal_cell_t* root, const char* path);

// Пометка успешной синхронизации по заданному пути.
void decimal_cell_mark_sync(decimal_cell_t* root,
                            const uint8_t* path,
                            size_t path_len,
                            uint64_t timestamp);

// Деактивация и освобождение поддерева по пути.
void decimal_cell_deactivate_path(decimal_cell_t* root,
                                  const uint8_t* path,
                                  size_t path_len,
                                  uint64_t timestamp);

// Обновление состояния узлов (рекурсивно) с учётом таймаутов синхронизации.
void decimal_cell_update_state(decimal_cell_t* cell, uint64_t now);

// Получение списка активных дочерних цифр первого уровня.
size_t decimal_cell_collect_active_children(const decimal_cell_t* cell,
                                            uint8_t* out_digits,
                                            size_t max_digits);

// Простая сериализация дерева в текстовом виде.
size_t decimal_cell_serialize(const decimal_cell_t* cell, char* buffer, size_t buffer_size);

#endif
