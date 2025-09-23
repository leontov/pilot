#include <string.h>
#include "kolibri_decimal_cell.h"
#include "kolibri_ping.h"

void init_decimal_cell(decimal_cell_t* cell, uint8_t digit) {
    if (digit > 9) digit = digit % 10;
    cell->node_digit = digit;
    cell->n_neighbors = 0;
    memset(cell->neighbor_digits, 0, sizeof(cell->neighbor_digits));
    memset(cell->last_sync, 0, sizeof(cell->last_sync));
    memset(cell->is_active, 0, sizeof(cell->is_active));
}

void cleanup_decimal_cell(decimal_cell_t* cell) {
    if (cell) {
        cell->node_digit = 0;
        cell->n_neighbors = 0;
        memset(cell->neighbor_digits, 0, sizeof(cell->neighbor_digits));
        memset(cell->last_sync, 0, sizeof(cell->last_sync));
        memset(cell->is_active, 0, sizeof(cell->is_active));
    }
}

int add_neighbor(decimal_cell_t* cell, uint8_t digit) {
    if (digit > 9) digit = digit % 10;
    if (digit == cell->node_digit) return -1;  // нельзя добавить себя как соседа
    
    // Проверяем, нет ли уже такого соседа
    for (int i = 0; i < cell->n_neighbors; i++) {
        if (cell->neighbor_digits[i] == digit) {
            return i;  // сосед уже существует
        }
    }
    
    if (cell->n_neighbors >= MAX_NEIGHBORS) return -1;
    
    int idx = cell->n_neighbors;
    cell->neighbor_digits[idx] = digit;
    cell->last_sync[idx] = now_ms();
    cell->is_active[idx] = true;
    cell->n_neighbors++;
    
    return idx;
}

void remove_neighbor(decimal_cell_t* cell, uint8_t digit) {
    int idx = get_neighbor_index(cell, digit);
    if (idx < 0) return;
    
    // Сдвигаем остальных соседей
    for (int i = idx; i < cell->n_neighbors - 1; i++) {
        cell->neighbor_digits[i] = cell->neighbor_digits[i + 1];
        cell->last_sync[i] = cell->last_sync[i + 1];
        cell->is_active[i] = cell->is_active[i + 1];
    }
    
    cell->n_neighbors--;
}

bool needs_sync(decimal_cell_t* cell, uint8_t neighbor_idx) {
    if (neighbor_idx >= cell->n_neighbors) return false;
    if (!cell->is_active[neighbor_idx]) return false;
    
    uint64_t now = now_ms();
    return (now - cell->last_sync[neighbor_idx]) >= SYNC_INTERVAL;
}

void mark_sync(decimal_cell_t* cell, uint8_t neighbor_idx) {
    if (neighbor_idx >= cell->n_neighbors) return;
    cell->last_sync[neighbor_idx] = now_ms();
    cell->is_active[neighbor_idx] = true;
}

bool is_neighbor_active(decimal_cell_t* cell, uint8_t neighbor_idx) {
    if (neighbor_idx >= cell->n_neighbors) return false;
    return cell->is_active[neighbor_idx];
}

int get_neighbor_index(decimal_cell_t* cell, uint8_t digit) {
    if (digit > 9) digit = digit % 10;
    for (int i = 0; i < cell->n_neighbors; i++) {
        if (cell->neighbor_digits[i] == digit) {
            return i;
        }
    }
    return -1;
}

void update_cell_state(decimal_cell_t* cell) {
    uint64_t now = now_ms();
    
    // Проверяем активность соседей
    for (int i = 0; i < cell->n_neighbors; i++) {
        if (now - cell->last_sync[i] > SYNC_INTERVAL * 3) {
            cell->is_active[i] = false;
        }
    }
}
