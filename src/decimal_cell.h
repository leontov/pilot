#ifndef DECIMAL_CELL_H
#define DECIMAL_CELL_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    double value;           // Текущее значение ячейки
    double min_value;      // Минимальное значение
    double max_value;      // Максимальное значение
    uint32_t connections;  // Битовая маска соединений
    bool active;           // Флаг активности
} DecimalCell;

// Инициализация новой ячейки
DecimalCell* decimal_cell_create(double initial_value, double min, double max);

// Обновление значения с проверкой границ
bool decimal_cell_update(DecimalCell* cell, double new_value);

// Добавление связи с другой ячейкой
bool decimal_cell_connect(DecimalCell* cell, uint32_t other_cell_id);

// Удаление связи
bool decimal_cell_disconnect(DecimalCell* cell, uint32_t other_cell_id);

// Активация/деактивация ячейки
void decimal_cell_set_active(DecimalCell* cell, bool active);

// Освобождение ресурсов
void decimal_cell_destroy(DecimalCell* cell);

#endif // DECIMAL_CELL_H
