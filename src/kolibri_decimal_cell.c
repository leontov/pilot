#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kolibri_decimal_cell.h"
#include "kolibri_ping.h"

static decimal_cell_t* create_child(decimal_cell_t* parent, uint8_t digit) {
    decimal_cell_t* child = (decimal_cell_t*)calloc(1, sizeof(decimal_cell_t));
    if (!child) {
        return NULL;
    }
    uint64_t now = now_ms();
    child->digit = digit % DECIMAL_CELL_FANOUT;
    child->depth = parent ? (uint8_t)(parent->depth + 1) : 0;
    child->is_active = true;
    child->created_at = now;
    child->last_state_change = now;
    child->last_sync_time = now;
    child->sync_interval = parent ? parent->sync_interval : SYNC_INTERVAL;
    child->parent = parent;
    for (int i = 0; i < DECIMAL_CELL_FANOUT; i++) {
        child->children[i] = NULL;
        child->child_active[i] = false;
        child->child_last_sync[i] = now;
        child->child_last_state_change[i] = now;
    }
    return child;
}

static void free_subtree(decimal_cell_t* node) {
    if (!node) return;
    for (int i = 0; i < DECIMAL_CELL_FANOUT; i++) {
        if (node->children[i]) {
            free_subtree(node->children[i]);
            node->children[i] = NULL;
        }
    }
    free(node);
}

static bool node_has_children(const decimal_cell_t* node) {
    if (!node) return false;
    for (uint8_t d = 0; d < DECIMAL_CELL_FANOUT; d++) {
        if (node->children[d]) {
            return true;
        }
    }
    return false;
}

static bool deactivate_recursive(decimal_cell_t* node,
                                 const uint8_t* path,
                                 size_t path_len,
                                 uint64_t timestamp) {
    if (!node) return false;
    if (path_len == 0) {
        for (uint8_t d = 0; d < DECIMAL_CELL_FANOUT; d++) {
            if (node->children[d]) {
                free_subtree(node->children[d]);
                node->children[d] = NULL;
            }
            node->child_active[d] = false;
            node->child_last_sync[d] = timestamp;
            node->child_last_state_change[d] = timestamp;
        }
        node->is_active = false;
        node->last_state_change = timestamp;
        node->last_sync_time = timestamp;
        return true;
    }

    uint8_t digit = path[0] % DECIMAL_CELL_FANOUT;
    decimal_cell_t* child = node->children[digit];
    if (!child) {
        return false;
    }

    bool remove_child = false;
    if (path_len == 1) {
        free_subtree(child);
        remove_child = true;
    } else {
        if (deactivate_recursive(child, path + 1, path_len - 1, timestamp)) {
            free(child);
            remove_child = true;
        }
    }

    if (remove_child) {
        node->children[digit] = NULL;
    }
    node->child_active[digit] = false;
    node->child_last_sync[digit] = timestamp;
    node->child_last_state_change[digit] = timestamp;

    if (remove_child) {
        if (!node->is_active && !node_has_children(node)) {
            node->last_state_change = timestamp;
            return true;
        }
    }
    return false;
}

static size_t append_fmt(char* buffer, size_t size, size_t used, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap_copy;
    va_copy(ap_copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap_copy);
    va_end(ap_copy);
    if (needed < 0) {
        va_end(ap);
        return used;
    }
    if (used < size) {
        size_t space = size - used;
        if (space == 0) {
            // nothing to write but still consume the formatted length
        } else {
            vsnprintf(buffer + used, space, fmt, ap);
        }
    }
    va_end(ap);
    return used + (size_t)needed;
}

static size_t serialize_node_internal(const decimal_cell_t* node,
                                      char* buffer,
                                      size_t size,
                                      size_t used) {
    if (!node) return used;
    used = append_fmt(buffer, size, used,
                      "{\"digit\":%u,\"depth\":%u,\"active\":%s",
                      node->digit,
                      node->depth,
                      node->is_active ? "true" : "false");
    bool has_child = false;
    for (uint8_t d = 0; d < DECIMAL_CELL_FANOUT; d++) {
        decimal_cell_t* child = node->children[d];
        if (!child) continue;
        if (!has_child) {
            used = append_fmt(buffer, size, used, ",\"children\":[");
            has_child = true;
        } else {
            used = append_fmt(buffer, size, used, ",");
        }
        used = append_fmt(buffer, size, used,
                          "{\"edge_digit\":%u,\"edge_active\":%s,\"node\":",
                          d,
                          node->child_active[d] ? "true" : "false");
        used = serialize_node_internal(child, buffer, size, used);
        used = append_fmt(buffer, size, used, "}");
    }
    if (has_child) {
        used = append_fmt(buffer, size, used, "]");
    }
    used = append_fmt(buffer, size, used, "}");
    return used;
}

void init_decimal_cell(decimal_cell_t* cell, uint8_t digit) {
    if (!cell) return;
    uint64_t now = now_ms();
    memset(cell, 0, sizeof(*cell));
    cell->digit = digit % DECIMAL_CELL_FANOUT;
    cell->depth = 0;
    cell->is_active = true;
    cell->created_at = now;
    cell->last_state_change = now;
    cell->last_sync_time = now;
    cell->sync_interval = SYNC_INTERVAL;
    cell->parent = NULL;
    for (int i = 0; i < DECIMAL_CELL_FANOUT; i++) {
        cell->children[i] = NULL;
        cell->child_active[i] = false;
        cell->child_last_sync[i] = now;
        cell->child_last_state_change[i] = now;
    }
}

void cleanup_decimal_cell(decimal_cell_t* cell) {
    if (!cell) return;
    for (int i = 0; i < DECIMAL_CELL_FANOUT; i++) {
        if (cell->children[i]) {
            free_subtree(cell->children[i]);
            cell->children[i] = NULL;
        }
        cell->child_active[i] = false;
        cell->child_last_sync[i] = 0;
        cell->child_last_state_change[i] = 0;
    }
    cell->is_active = false;
    cell->last_state_change = 0;
    cell->last_sync_time = 0;
}

decimal_cell_t* decimal_cell_add_path(decimal_cell_t* root,
                                      const uint8_t* path,
                                      size_t path_len,
                                      bool activate) {
    if (!root) return NULL;
    decimal_cell_t* current = root;
    uint64_t now = now_ms();
    if (path_len == 0) {
        current->is_active = activate;
        current->last_state_change = now;
        current->last_sync_time = now;
        return current;
    }
    for (size_t i = 0; i < path_len; i++) {
        uint8_t digit = path[i] % DECIMAL_CELL_FANOUT;
        if (!current->children[digit]) {
            decimal_cell_t* child = create_child(current, digit);
            if (!child) {
                return NULL;
            }
            current->children[digit] = child;
        }
        current->child_active[digit] = activate;
        current->child_last_sync[digit] = now;
        current->child_last_state_change[digit] = now;
        current->last_sync_time = now;
        current = current->children[digit];
        current->is_active = activate;
        current->last_sync_time = now;
        current->last_state_change = now;
    }
    return current;
}

decimal_cell_t* decimal_cell_add_path_str(decimal_cell_t* root,
                                          const char* path,
                                          bool activate) {
    if (!root || !path) return NULL;
    size_t len = strlen(path);
    if (len == 0) {
        return decimal_cell_add_path(root, NULL, 0, activate);
    }
    uint8_t stack_buf[64];
    uint8_t* digits = stack_buf;
    bool heap = false;
    if (len > sizeof(stack_buf)) {
        digits = (uint8_t*)malloc(len);
        if (!digits) {
            return NULL;
        }
        heap = true;
    }
    size_t count = 0;
    for (size_t i = 0; i < len; i++) {
        if (isdigit((unsigned char)path[i])) {
            digits[count++] = (uint8_t)(path[i] - '0');
        }
    }
    decimal_cell_t* node = decimal_cell_add_path(root, digits, count, activate);
    if (heap) {
        free(digits);
    }
    return node;
}

decimal_cell_t* decimal_cell_find_path(decimal_cell_t* root,
                                       const uint8_t* path,
                                       size_t path_len) {
    if (!root) return NULL;
    if (path_len == 0) return root;
    decimal_cell_t* current = root;
    for (size_t i = 0; i < path_len; i++) {
        uint8_t digit = path[i] % DECIMAL_CELL_FANOUT;
        if (!current->children[digit]) {
            return NULL;
        }
        current = current->children[digit];
    }
    return current;
}

decimal_cell_t* decimal_cell_find_path_str(decimal_cell_t* root, const char* path) {
    if (!root || !path) return NULL;
    size_t len = strlen(path);
    if (len == 0) return root;
    uint8_t stack_buf[64];
    uint8_t* digits = stack_buf;
    bool heap = false;
    if (len > sizeof(stack_buf)) {
        digits = (uint8_t*)malloc(len);
        if (!digits) {
            return NULL;
        }
        heap = true;
    }
    size_t count = 0;
    for (size_t i = 0; i < len; i++) {
        if (isdigit((unsigned char)path[i])) {
            digits[count++] = (uint8_t)(path[i] - '0');
        }
    }
    decimal_cell_t* node = decimal_cell_find_path(root, digits, count);
    if (heap) {
        free(digits);
    }
    return node;
}

void decimal_cell_mark_sync(decimal_cell_t* root,
                            const uint8_t* path,
                            size_t path_len,
                            uint64_t timestamp) {
    if (!root) return;
    if (path_len == 0) {
        root->last_sync_time = timestamp;
        root->is_active = true;
        root->last_state_change = timestamp;
        return;
    }
    decimal_cell_t* current = root;
    for (size_t i = 0; i < path_len; i++) {
        uint8_t digit = path[i] % DECIMAL_CELL_FANOUT;
        if (!current->children[digit]) {
            return;
        }
        current->child_last_sync[digit] = timestamp;
        current->child_last_state_change[digit] = timestamp;
        current->child_active[digit] = true;
        current->last_sync_time = timestamp;
        current = current->children[digit];
        current->is_active = true;
        current->last_sync_time = timestamp;
        current->last_state_change = timestamp;
    }
}

void decimal_cell_deactivate_path(decimal_cell_t* root,
                                  const uint8_t* path,
                                  size_t path_len,
                                  uint64_t timestamp) {
    if (!root) return;
    if (path_len == 0) {
        deactivate_recursive(root, NULL, 0, timestamp);
        return;
    }
    deactivate_recursive(root, path, path_len, timestamp);
}

void decimal_cell_update_state(decimal_cell_t* cell, uint64_t now) {
    if (!cell) return;
    if (cell->is_active && (now - cell->last_sync_time) > cell->sync_interval * 3) {
        cell->is_active = false;
        cell->last_state_change = now;
    }
    for (uint8_t d = 0; d < DECIMAL_CELL_FANOUT; d++) {
        if (!cell->children[d]) continue;
        if (cell->child_active[d] && (now - cell->child_last_sync[d]) > cell->sync_interval * 3) {
            cell->child_active[d] = false;
            cell->child_last_state_change[d] = now;
        }
        decimal_cell_update_state(cell->children[d], now);
    }
}

size_t decimal_cell_collect_active_children(const decimal_cell_t* cell,
                                            uint8_t* out_digits,
                                            size_t max_digits) {
    if (!cell || !out_digits || max_digits == 0) return 0;
    size_t count = 0;
    for (uint8_t d = 0; d < DECIMAL_CELL_FANOUT && count < max_digits; d++) {
        if (cell->children[d] && cell->child_active[d]) {
            out_digits[count++] = d;
        }
    }
    return count;
}

size_t decimal_cell_serialize(const decimal_cell_t* cell, char* buffer, size_t buffer_size) {
    if (!cell) {
        if (buffer_size > 0) buffer[0] = '\0';
        return 0;
    }
    size_t total = serialize_node_internal(cell, buffer, buffer_size, 0);
    if (buffer_size > 0) {
        size_t term = (total < buffer_size - 1) ? total : (buffer_size - 1);
        buffer[term] = '\0';
    }
    return total;
}
