#ifndef KOLIBRI_DECIMAL_CELL_H
#define KOLIBRI_DECIMAL_CELL_H

#include <stdbool.h>
#include <stddef.h>

} decimal_cell_t;

// Инициализация корня десятичного дерева.
void init_decimal_cell(decimal_cell_t* cell, uint8_t digit);

// Полная очистка дерева (рекурсивное освобождение всех дочерних узлов).
void cleanup_decimal_cell(decimal_cell_t* cell);


#define MAX_NEIGHBORS (DECIMAL_BRANCHING - 1)

#define DECIMAL_PATH_DIGIT(d) ((uint8_t)((d) % DECIMAL_BRANCHING))

#endif
