#ifndef KOLIBRI_DECIMAL_CELL_H
#define KOLIBRI_DECIMAL_CELL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define DECIMAL_BRANCHING 10
#define SYNC_INTERVAL 1000  // интервал синхронизации в мс

typedef struct decimal_cell {
    uint8_t node_digit;                       // собственная цифра узла (0-9)
    uint8_t depth;                            // текущая глубина ветви
    bool initialized;                         // флаг инициализации для безопасной очистки
    struct decimal_cell* children[DECIMAL_BRANCHING];  // дочерние узлы по цифрам 0-9
    uint64_t last_sync[DECIMAL_BRANCHING];    // время последней синхронизации с дочерними ветвями
    bool is_active[DECIMAL_BRANCHING];        // активность дочерних ветвей
} decimal_cell_t;

// Инициализация узла с его цифрой
void init_decimal_cell(decimal_cell_t* cell, uint8_t digit);

// Очистка ресурсов узла
void cleanup_decimal_cell(decimal_cell_t* cell);

// Траверс по пути цифр. При create = true недостающие узлы создаются.
decimal_cell_t* decimal_cell_traverse(decimal_cell_t* root, const uint8_t* path, size_t length, bool create);

// Активация/деактивация ветви по пути цифр
bool decimal_cell_set_active(decimal_cell_t* root, const uint8_t* path, size_t length, bool active);

// Маркировка синхронизации ветви
bool decimal_cell_mark_sync(decimal_cell_t* root, const uint8_t* path, size_t length, uint64_t now);

// Проверка необходимости синхронизации для ветви
bool decimal_cell_needs_sync(const decimal_cell_t* root, const uint8_t* path, size_t length, uint64_t now);

// Удаление ветви и всей ее подструктуры
bool decimal_cell_remove_branch(decimal_cell_t* root, const uint8_t* path, size_t length);

// Получение списка дочерних цифр (активных или всех)
size_t decimal_cell_collect_children(const decimal_cell_t* cell, uint8_t* out_digits, size_t max_out, bool active_only);

// Проверка активности и получение времени синхронизации дочерней цифры
bool decimal_cell_child_is_active(const decimal_cell_t* cell, uint8_t digit);
uint64_t decimal_cell_child_last_sync(const decimal_cell_t* cell, uint8_t digit);

// Подсчет дочерних узлов
size_t decimal_cell_child_count(const decimal_cell_t* cell, bool active_only);

// Обновление состояния ячейки и всех дочерних ветвей
void update_cell_state(decimal_cell_t* cell);

#define MAX_NEIGHBORS (DECIMAL_BRANCHING - 1)

#define DECIMAL_PATH_DIGIT(d) ((uint8_t)((d) % DECIMAL_BRANCHING))

#endif
