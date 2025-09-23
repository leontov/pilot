#include "kolibri_decimal_cell.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void reset_child_metadata(decimal_cell_t* cell) {
    if (!cell) {
        return;
    }
    for (size_t i = 0; i < DECIMAL_CELL_FANOUT; ++i) {
        cell->children[i] = NULL;
        cell->child_active[i] = false;
        cell->child_last_state_change[i] = 0;
        cell->child_last_sync[i] = 0;
    }
}

static void init_child(decimal_cell_t* child, decimal_cell_t* parent,
                       uint8_t digit) {
    if (!child) {
        return;
    }

    child->digit = digit;
    child->depth = parent ? (uint8_t)(parent->depth + 1) : 0;
    child->is_active = false;
    child->last_state_change = 0;
    child->last_sync_time = 0;
    child->sync_interval = 0;
    child->parent = parent;
    reset_child_metadata(child);
}

void init_decimal_cell(decimal_cell_t* cell, uint8_t digit) {
    if (!cell) {
        return;
    }

    cell->digit = digit;
    cell->depth = 0;
    cell->is_active = false;
    cell->last_state_change = 0;
    cell->last_sync_time = 0;
    cell->sync_interval = 0;
    cell->parent = NULL;
    reset_child_metadata(cell);
}

static void destroy_children(decimal_cell_t* cell) {
    if (!cell) {
        return;
    }

    for (size_t i = 0; i < DECIMAL_CELL_FANOUT; ++i) {
        decimal_cell_t* child = cell->children[i];
        if (child) {
            destroy_children(child);
            free(child);
            cell->children[i] = NULL;
        }
        cell->child_active[i] = false;
        cell->child_last_state_change[i] = 0;
        cell->child_last_sync[i] = 0;
    }
}

void cleanup_decimal_cell(decimal_cell_t* cell) {
    if (!cell) {
        return;
    }

    destroy_children(cell);
    cell->is_active = false;
    cell->last_state_change = 0;
    cell->last_sync_time = 0;
    cell->sync_interval = 0;
}

decimal_cell_t* decimal_cell_get_child(decimal_cell_t* cell, uint8_t digit,
                                       bool create_if_missing) {
    if (!cell || digit >= DECIMAL_CELL_FANOUT) {
        return NULL;
    }

    decimal_cell_t* child = cell->children[digit];
    if (!child && create_if_missing) {
        child = (decimal_cell_t*)calloc(1, sizeof(decimal_cell_t));
        if (!child) {
            return NULL;
        }
        init_child(child, cell, digit);
        cell->children[digit] = child;
        cell->child_active[digit] = false;
        cell->child_last_state_change[digit] = 0;
        cell->child_last_sync[digit] = 0;
    }
    return child;
}

void decimal_cell_mark_active(decimal_cell_t* cell, bool active,
                              uint64_t timestamp) {
    if (!cell) {
        return;
    }

    if (cell->is_active != active) {
        cell->is_active = active;
        cell->last_state_change = timestamp;
    }

    if (cell->parent) {
        decimal_cell_t* parent = cell->parent;
        uint8_t digit = cell->digit;
        parent->child_active[digit] = cell->is_active;
        parent->child_last_state_change[digit] = timestamp;
    }
}

void decimal_cell_mark_sync(decimal_cell_t* cell, const uint8_t* path,
                            size_t path_len, uint64_t timestamp) {
    if (!cell) {
        return;
    }

    uint64_t previous_sync = cell->last_sync_time;
    cell->last_sync_time = timestamp;
    if (previous_sync != 0 && timestamp >= previous_sync) {
        cell->sync_interval = timestamp - previous_sync;
    }

    decimal_cell_t* current = cell;
    for (size_t i = 0; i < path_len; ++i) {
        uint8_t digit = path[i];
        if (digit >= DECIMAL_CELL_FANOUT) {
            break;
        }

        decimal_cell_t* child = decimal_cell_get_child(current, digit, true);
        if (!child) {
            break;
        }

        previous_sync = current->child_last_sync[digit];
        current->child_last_sync[digit] = timestamp;
        current->child_active[digit] = child->is_active;
        if (previous_sync != 0 && timestamp >= previous_sync) {
            uint64_t delta = timestamp - previous_sync;
            child->sync_interval = delta;
        }

        current = child;
        previous_sync = current->last_sync_time;
        current->last_sync_time = timestamp;
        if (previous_sync != 0 && timestamp >= previous_sync) {
            current->sync_interval = timestamp - previous_sync;
        }
    }
}

size_t decimal_cell_collect_active_children(const decimal_cell_t* cell,
                                            uint8_t* out_digits,
                                            size_t max_digits) {
    if (!cell) {
        return 0;
    }

    size_t count = 0;
    for (uint8_t digit = 0; digit < DECIMAL_CELL_FANOUT; ++digit) {
        const decimal_cell_t* child = cell->children[digit];
        bool is_active = cell->child_active[digit] || (child && child->is_active);
        if (!is_active) {
            continue;
        }
        if (out_digits && count < max_digits) {
            out_digits[count] = digit;
        }
        ++count;
    }
    return count;
}

static size_t append_format(char* buffer, size_t buffer_size, size_t offset,
                            const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    size_t written = offset;
    if (buffer && offset < buffer_size) {
        int ret = vsnprintf(buffer + offset, buffer_size - offset, fmt, args);
        if (ret > 0) {
            written += (size_t)ret;
        }
    } else {
        int ret = vsnprintf(NULL, 0, fmt, args);
        if (ret > 0) {
            written += (size_t)ret;
        }
    }

    va_end(args);
    return written;
}

static size_t serialize_node_internal(const decimal_cell_t* cell, char* buffer,
                                      size_t buffer_size, size_t offset) {
    if (!cell) {
        return offset;
    }

    offset = append_format(buffer, buffer_size, offset,
                           "{\"digit\":%u,\"depth\":%u,\"active\":%s,"
                           "\"last_state_change\":%llu,\"last_sync\":%llu,"
                           "\"sync_interval\":%llu",
                           cell->digit,
                           cell->depth,
                           cell->is_active ? "true" : "false",
                           (unsigned long long)cell->last_state_change,
                           (unsigned long long)cell->last_sync_time,
                           (unsigned long long)cell->sync_interval);

    bool has_children = false;
    for (uint8_t digit = 0; digit < DECIMAL_CELL_FANOUT; ++digit) {
        const decimal_cell_t* child = cell->children[digit];
        if (!child) {
            continue;
        }
        if (!has_children) {
            offset = append_format(buffer, buffer_size, offset,
                                   ",\"children\":[");
            has_children = true;
        } else {
            offset = append_format(buffer, buffer_size, offset, ",");
        }
        offset = serialize_node_internal(child, buffer, buffer_size, offset);
    }

    if (has_children) {
        offset = append_format(buffer, buffer_size, offset, "]");
    }

    offset = append_format(buffer, buffer_size, offset, "}");
    return offset;
}

size_t decimal_cell_serialize(const decimal_cell_t* cell, char* buffer,
                              size_t buffer_size) {
    if (!cell) {
        if (buffer_size > 0 && buffer) {
            buffer[0] = '\0';
        }
        return 0;
    }

    size_t total = serialize_node_internal(cell, buffer, buffer_size, 0);
    if (buffer && buffer_size > 0) {
        size_t term = (total < buffer_size - 1) ? total : (buffer_size - 1);
        buffer[term] = '\0';
    }
    return total;
}
