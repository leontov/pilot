#include "decimal_cell.h"
#include <stdlib.h>

DecimalCell* decimal_cell_create(double initial_value, double min, double max) {
    DecimalCell* cell = (DecimalCell*)malloc(sizeof(DecimalCell));
    if (!cell) return NULL;
    
    cell->value = initial_value;
    cell->min_value = min;
    cell->max_value = max;
    cell->connections = 0;
    cell->active = true;
    
    return cell;
}

bool decimal_cell_update(DecimalCell* cell, double new_value) {
    if (!cell || !cell->active) return false;
    
    if (new_value < cell->min_value) {
        cell->value = cell->min_value;
        return false;
    }
    
    if (new_value > cell->max_value) {
        cell->value = cell->max_value;
        return false;
    }
    
    cell->value = new_value;
    return true;
}

bool decimal_cell_connect(DecimalCell* cell, uint32_t other_cell_id) {
    if (!cell || !cell->active) return false;
    
    // Устанавливаем бит соединения
    cell->connections |= (1U << other_cell_id);
    return true;
}

bool decimal_cell_disconnect(DecimalCell* cell, uint32_t other_cell_id) {
    if (!cell || !cell->active) return false;
    
    // Сбрасываем бит соединения
    cell->connections &= ~(1U << other_cell_id);
    return true;
}

void decimal_cell_set_active(DecimalCell* cell, bool active) {
    if (cell) {
        cell->active = active;
    }
}

void decimal_cell_destroy(DecimalCell* cell) {
    free(cell);
}
