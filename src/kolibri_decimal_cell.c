#include <stdlib.h>
#include <string.h>

#include "kolibri_decimal_cell.h"
#include "kolibri_ping.h"

static void decimal_cell_recursive_cleanup(decimal_cell_t* cell);
static decimal_cell_t* decimal_cell_create_child(decimal_cell_t* parent, uint8_t digit);
static bool decimal_cell_resolve_branch(decimal_cell_t* root, const uint8_t* path, size_t length,
                                        bool create, decimal_cell_t** out_parent,
                                        uint8_t* out_digit, decimal_cell_t** out_child);

void init_decimal_cell(decimal_cell_t* cell, uint8_t digit) {
    if (!cell) {
        return;
    }

    if (cell->initialized) {
        cleanup_decimal_cell(cell);
    }

    memset(cell, 0, sizeof(*cell));
    cell->node_digit = DECIMAL_PATH_DIGIT(digit);
    cell->depth = 0;
    cell->initialized = true;
}

static void decimal_cell_recursive_cleanup(decimal_cell_t* cell) {
    if (!cell) {
        return;
    }

    for (uint8_t d = 0; d < DECIMAL_BRANCHING; ++d) {
        decimal_cell_t* child = cell->children[d];
        if (child) {
            decimal_cell_recursive_cleanup(child);
            free(child);
        }
        cell->children[d] = NULL;
        cell->is_active[d] = false;
        cell->last_sync[d] = 0;
    }
}

void cleanup_decimal_cell(decimal_cell_t* cell) {
    if (!cell || !cell->initialized) {
        return;
    }

    decimal_cell_recursive_cleanup(cell);
    cell->initialized = false;
    cell->depth = 0;
    cell->node_digit = 0;
}

static decimal_cell_t* decimal_cell_create_child(decimal_cell_t* parent, uint8_t digit) {
    decimal_cell_t* child = calloc(1, sizeof(decimal_cell_t));
    if (!child) {
        return NULL;
    }

    child->node_digit = DECIMAL_PATH_DIGIT(digit);
    child->depth = parent ? (uint8_t)(parent->depth + 1) : 0;
    child->initialized = true;
    if (parent) {
        parent->children[child->node_digit] = child;
        parent->is_active[child->node_digit] = true;
        parent->last_sync[child->node_digit] = now_ms();
    }
    return child;
}

decimal_cell_t* decimal_cell_traverse(decimal_cell_t* root, const uint8_t* path, size_t length, bool create) {
    if (!root) {
        return NULL;
    }

    decimal_cell_t* current = root;
    for (size_t i = 0; i < length; ++i) {
        uint8_t digit = DECIMAL_PATH_DIGIT(path[i]);
        decimal_cell_t* next = current->children[digit];
        if (!next) {
            if (!create) {
                return NULL;
            }
            next = decimal_cell_create_child(current, digit);
            if (!next) {
                return NULL;
            }
        }
        current = next;
    }
    return current;
}

static bool decimal_cell_resolve_branch(decimal_cell_t* root, const uint8_t* path, size_t length,
                                        bool create, decimal_cell_t** out_parent,
                                        uint8_t* out_digit, decimal_cell_t** out_child) {
    if (!root || length == 0) {
        return false;
    }

    decimal_cell_t* parent = (length > 1)
                                 ? decimal_cell_traverse(root, path, length - 1, create)
                                 : root;
    if (!parent) {
        return false;
    }

    uint8_t digit = DECIMAL_PATH_DIGIT(path[length - 1]);
    decimal_cell_t* child = parent->children[digit];
    if (!child && create) {
        child = decimal_cell_create_child(parent, digit);
    }

    if (!child) {
        return false;
    }

    if (out_parent) {
        *out_parent = parent;
    }
    if (out_digit) {
        *out_digit = digit;
    }
    if (out_child) {
        *out_child = child;
    }
    return true;
}

bool decimal_cell_set_active(decimal_cell_t* root, const uint8_t* path, size_t length, bool active) {
    decimal_cell_t* parent = NULL;
    uint8_t digit = 0;
    decimal_cell_t* child = NULL;

    if (!decimal_cell_resolve_branch(root, path, length, active, &parent, &digit, &child)) {
        return false;
    }

    if (!parent) {
        return false;
    }

    parent->is_active[digit] = active;
    if (active) {
        parent->last_sync[digit] = now_ms();
    }
    return true;
}

bool decimal_cell_mark_sync(decimal_cell_t* root, const uint8_t* path, size_t length, uint64_t now) {
    decimal_cell_t* parent = NULL;
    uint8_t digit = 0;
    decimal_cell_t* child = NULL;

    if (!decimal_cell_resolve_branch(root, path, length, false, &parent, &digit, &child)) {
        return false;
    }

    if (!parent) {
        return false;
    }

    parent->last_sync[digit] = now;
    parent->is_active[digit] = true;
    return true;
}

bool decimal_cell_needs_sync(const decimal_cell_t* root, const uint8_t* path, size_t length, uint64_t now) {
    if (!root || length == 0) {
        return false;
    }

    const decimal_cell_t* parent = root;
    if (length > 1) {
        for (size_t i = 0; i < length - 1 && parent; ++i) {
            uint8_t digit = DECIMAL_PATH_DIGIT(path[i]);
            parent = parent->children[digit];
        }
    }

    if (!parent) {
        return false;
    }

    uint8_t digit = DECIMAL_PATH_DIGIT(path[length - 1]);
    if (!parent->children[digit] || !parent->is_active[digit]) {
        return false;
    }

    return (now - parent->last_sync[digit]) >= SYNC_INTERVAL;
}

bool decimal_cell_remove_branch(decimal_cell_t* root, const uint8_t* path, size_t length) {
    decimal_cell_t* parent = NULL;
    uint8_t digit = 0;
    decimal_cell_t* child = NULL;

    if (!decimal_cell_resolve_branch(root, path, length, false, &parent, &digit, &child)) {
        return false;
    }

    decimal_cell_recursive_cleanup(child);
    free(child);
    parent->children[digit] = NULL;
    parent->is_active[digit] = false;
    parent->last_sync[digit] = 0;
    return true;
}

size_t decimal_cell_collect_children(const decimal_cell_t* cell, uint8_t* out_digits, size_t max_out, bool active_only) {
    if (!cell) {
        return 0;
    }

    size_t count = 0;
    for (uint8_t d = 0; d < DECIMAL_BRANCHING && count < max_out; ++d) {
        if (!cell->children[d]) {
            continue;
        }
        if (active_only && !cell->is_active[d]) {
            continue;
        }
        out_digits[count++] = d;
    }
    return count;
}

bool decimal_cell_child_is_active(const decimal_cell_t* cell, uint8_t digit) {
    if (!cell) {
        return false;
    }
    uint8_t d = DECIMAL_PATH_DIGIT(digit);
    return cell->children[d] != NULL && cell->is_active[d];
}

uint64_t decimal_cell_child_last_sync(const decimal_cell_t* cell, uint8_t digit) {
    if (!cell) {
        return 0;
    }
    uint8_t d = DECIMAL_PATH_DIGIT(digit);
    if (!cell->children[d]) {
        return 0;
    }
    return cell->last_sync[d];
}

size_t decimal_cell_child_count(const decimal_cell_t* cell, bool active_only) {
    if (!cell) {
        return 0;
    }
    size_t count = 0;
    for (uint8_t d = 0; d < DECIMAL_BRANCHING; ++d) {
        if (!cell->children[d]) {
            continue;
        }
        if (active_only && !cell->is_active[d]) {
            continue;
        }
        ++count;
    }
    return count;
}

void update_cell_state(decimal_cell_t* cell) {
    if (!cell) {
        return;
    }

    uint64_t now = now_ms();

    for (uint8_t d = 0; d < DECIMAL_BRANCHING; ++d) {
        decimal_cell_t* child = cell->children[d];
        if (!child) {
            continue;
        }

        if (cell->is_active[d] && (now - cell->last_sync[d]) > (SYNC_INTERVAL * 3)) {
            cell->is_active[d] = false;
        }

        update_cell_state(child);
    }
}

