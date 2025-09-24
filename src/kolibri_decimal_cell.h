/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_DECIMAL_CELL_H
#define KOLIBRI_DECIMAL_CELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Kolibri nodes arrange themselves in a base-10 tree to track neighbours
// and synchronisation status.
#define DECIMAL_BRANCHING 10
#define DECIMAL_CELL_FANOUT DECIMAL_BRANCHING

#define MAX_NEIGHBORS (DECIMAL_BRANCHING - 1)

#define DECIMAL_PATH_DIGIT(d) ((uint8_t)((d) % DECIMAL_BRANCHING))

typedef struct decimal_cell {
    uint8_t digit;                     // Digit represented by this node.
    uint8_t depth;                     // Depth in the tree (root == 0).
    bool is_active;                    // Whether the node is considered active.

    uint64_t last_state_change;        // When active flag last changed.
    uint64_t last_sync_time;           // Last time we synchronised with this node.
    uint64_t sync_interval;            // Interval between the two most recent syncs.

    struct decimal_cell* parent;       // Parent in the decimal tree.
    struct decimal_cell* children[DECIMAL_CELL_FANOUT];

    bool child_active[DECIMAL_CELL_FANOUT];
    uint64_t child_last_state_change[DECIMAL_CELL_FANOUT];
    uint64_t child_last_sync[DECIMAL_CELL_FANOUT];
} decimal_cell_t;

// Initialise the root of the decimal tree with the provided digit.
void init_decimal_cell(decimal_cell_t* cell, uint8_t digit);

// Recursively free all dynamically allocated children and reset the node.
void cleanup_decimal_cell(decimal_cell_t* cell);

// Retrieve (optionally creating) the child for a specific digit.
decimal_cell_t* decimal_cell_get_child(decimal_cell_t* cell, uint8_t digit,
                                       bool create_if_missing);

// Mark a node as active/inactive and update parent bookkeeping.
void decimal_cell_mark_active(decimal_cell_t* cell, bool active,
                              uint64_t timestamp);

// Record a synchronisation event for the node along the provided path.
void decimal_cell_mark_sync(decimal_cell_t* cell, const uint8_t* path,
                            size_t path_len, uint64_t timestamp);

// Collect the digits of currently active children.
size_t decimal_cell_collect_active_children(const decimal_cell_t* cell,
                                            uint8_t* out_digits,
                                            size_t max_digits);

// Serialise the tree into a JSON-like textual representation.
size_t decimal_cell_serialize(const decimal_cell_t* cell, char* buffer,
                              size_t buffer_size);

#endif
