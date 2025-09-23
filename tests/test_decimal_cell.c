#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "../src/kolibri_decimal_cell.h"

uint64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

static void print_result(const char* name, int ok) {
    if (ok) {
        printf("OK: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
    }
}

int main(void) {
    int failures = 0;
    decimal_cell_t root;
    init_decimal_cell(&root, 5);

    uint8_t path123[] = {1, 2, 3};
    decimal_cell_t* node123 = decimal_cell_add_path(&root, path123, 3, true);
    int ok = (node123 != NULL && node123->depth == 3 && node123->digit == 3);
    print_result("add_path depth-3", ok);
    failures += ok ? 0 : 1;

    decimal_cell_t* found = decimal_cell_find_path(&root, path123, 3);
    ok = (found == node123);
    print_result("find_path existing", ok);
    failures += ok ? 0 : 1;

    decimal_cell_t* node478 = decimal_cell_add_path_str(&root, "478", true);
    ok = (node478 != NULL && node478->depth == 3 && node478->digit == 8);
    print_result("add_path_str", ok);
    failures += ok ? 0 : 1;

    decimal_cell_t* found_str = decimal_cell_find_path_str(&root, "478");
    ok = (found_str == node478);
    print_result("find_path_str", ok);
    failures += ok ? 0 : 1;

    uint8_t neighbors[DECIMAL_CELL_FANOUT];
    size_t neighbor_count = decimal_cell_collect_active_children(&root, neighbors, DECIMAL_CELL_FANOUT);
    ok = (neighbor_count == 2);
    print_result("collect_active_children", ok);
    failures += ok ? 0 : 1;

    uint8_t path478[] = {4, 7, 8};
    uint64_t deactivate_ts = now_ms();
    decimal_cell_deactivate_path(&root, path478, 3, deactivate_ts);
    found_str = decimal_cell_find_path_str(&root, "478");
    ok = (found_str == NULL);
    print_result("deactivate_path", ok);
    failures += ok ? 0 : 1;

    neighbor_count = decimal_cell_collect_active_children(&root, neighbors, DECIMAL_CELL_FANOUT);
    ok = (neighbor_count == 1 && neighbors[0] == 1);
    print_result("collect_after_deactivate", ok);
    failures += ok ? 0 : 1;

    uint64_t mark_time = now_ms();
    decimal_cell_mark_sync(&root, path123, 3, mark_time);
    ok = (root.child_last_sync[1] == mark_time && node123->last_sync_time == mark_time);
    print_result("mark_sync", ok);
    failures += ok ? 0 : 1;

    decimal_cell_update_state(&root, mark_time + root.sync_interval * 4);
    ok = (!root.child_active[1] && !node123->is_active);
    print_result("update_state_timeout", ok);
    failures += ok ? 0 : 1;

    char buffer[1024];
    decimal_cell_serialize(&root, buffer, sizeof(buffer));
    ok = (strstr(buffer, "\"digit\":5") != NULL && strstr(buffer, "\"children\"") != NULL);
    print_result("serialize_contains", ok);
    failures += ok ? 0 : 1;

    cleanup_decimal_cell(&root);
    int remaining = 0;
    for (uint8_t d = 0; d < DECIMAL_CELL_FANOUT; d++) {
        if (root.children[d]) remaining++;
    }
    ok = (remaining == 0);
    print_result("cleanup_no_children", ok);
    failures += ok ? 0 : 1;

    return failures == 0 ? 0 : 1;
}
