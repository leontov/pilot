#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "kolibri_decimal_cell.h"

static uint64_t fake_now = 1000;

uint64_t now_ms(void) {
    return fake_now;
}

static void advance_time(uint64_t delta) {
    fake_now += delta;
}

static void serialize_active_paths(decimal_cell_t* root, const decimal_cell_t* node,
                                   uint8_t* path, size_t depth,
                                   char* buffer, size_t bufsize) {
    uint8_t digits[DECIMAL_BRANCHING];
    size_t count = decimal_cell_collect_children(node, digits, DECIMAL_BRANCHING, false);
    for (size_t i = 0; i < count; ++i) {
        path[depth] = digits[i];
        decimal_cell_t* child = decimal_cell_traverse(root, path, depth + 1, false);
        if (!child) {
            continue;
        }

        if (decimal_cell_child_is_active(node, digits[i])) {
            char path_repr[16];
            for (size_t j = 0; j <= depth; ++j) {
                path_repr[j] = (char)('0' + path[j]);
            }
            path_repr[depth + 1] = '\0';

            if (buffer[0] != '\0') {
                strncat(buffer, " ", bufsize - strlen(buffer) - 1);
            }
            strncat(buffer, path_repr, bufsize - strlen(buffer) - 1);
        }

        serialize_active_paths(root, child, path, depth + 1, buffer, bufsize);
    }
}

static void test_create_and_traverse(void) {
    decimal_cell_t cell;
    init_decimal_cell(&cell, 5);

    uint8_t branch123[] = {1, 2, 3};
    assert(decimal_cell_set_active(&cell, branch123, 3, true));

    decimal_cell_t* node = decimal_cell_traverse(&cell, branch123, 3, false);
    assert(node != NULL);
    assert(node->depth == 3);
    assert(node->node_digit == 3);

    uint8_t branch7[] = {7};
    assert(decimal_cell_set_active(&cell, branch7, 1, true));

    uint8_t digits[DECIMAL_BRANCHING];
    size_t count = decimal_cell_collect_children(&cell, digits, DECIMAL_BRANCHING, true);
    assert(count == 2);
    assert(digits[0] == 1);
    assert(digits[1] == 7);

    char serialized[64] = {0};
    uint8_t path[8];
    serialize_active_paths(&cell, &cell, path, 0, serialized, sizeof(serialized));
    assert(strcmp(serialized, "1 12 123 7") == 0);

    decimal_cell_set_active(&cell, branch7, 1, false);
    assert(!decimal_cell_child_is_active(&cell, 7));

    decimal_cell_remove_branch(&cell, branch7, 1);
    count = decimal_cell_collect_children(&cell, digits, DECIMAL_BRANCHING, false);
    assert(count == 1);
    assert(digits[0] == 1);

    cleanup_decimal_cell(&cell);
}

static void test_sync_and_cleanup(void) {
    decimal_cell_t cell;
    init_decimal_cell(&cell, 2);

    uint8_t branch[] = {4};
    assert(decimal_cell_set_active(&cell, branch, 1, true));

    uint64_t now = now_ms();
    decimal_cell_mark_sync(&cell, branch, 1, now);

    advance_time(SYNC_INTERVAL * 4);
    update_cell_state(&cell);
    assert(!decimal_cell_child_is_active(&cell, 4));

    decimal_cell_remove_branch(&cell, branch, 1);
    assert(decimal_cell_child_count(&cell, false) == 0);

    cleanup_decimal_cell(&cell);
    assert(decimal_cell_child_count(&cell, false) == 0);
}

int main(void) {
    test_create_and_traverse();
    test_sync_and_cleanup();
    printf("All decimal cell tests passed!\n");
    return 0;
}

