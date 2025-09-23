#include <stdio.h>
#include <json-c/json.h>
#include "../src/rule_engine.h"

int main(void) {
    RuleEngine re;
    rule_engine_init(&re, "test_node");
    // ensure list returns array
    json_object *list = rule_engine_list(&re);
    if (!list || json_object_get_type(list) != json_type_array) {
        printf("FAIL: rule list not array\n");
        return 1;
    }
    json_object_put(list);
    // test sync with empty file
    FILE *f = fopen("cluster_rules.json", "w");
    fprintf(f, "[]"); fclose(f);
    int added = rule_engine_sync(&re, "cluster_rules.json");
    printf("sync added=%d\n", added);
    rule_engine_free(&re);
    return 0;
}
