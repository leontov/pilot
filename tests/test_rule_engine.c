#include <stdio.h>
#include <json-c/json.h>
#include <string.h>
#include "../src/rule_engine.h"
#include "../src/formula.h"

static int test_formula_collection_remove(void) {
    FormulaCollection *collection = formula_collection_create(2);
    if (!collection) {
        printf("FAIL: unable to create collection\n");
        return 1;
    }

    Formula f1 = {0};
    Formula f2 = {0};
    Formula f3 = {0};

    snprintf(f1.id, sizeof(f1.id), "id-1");
    snprintf(f2.id, sizeof(f2.id), "id-2");
    snprintf(f3.id, sizeof(f3.id), "id-3");

    snprintf(f1.content, sizeof(f1.content), "f(x) = x");
    snprintf(f2.content, sizeof(f2.content), "f(x) = 2 * x");
    snprintf(f3.content, sizeof(f3.content), "f(x) = 3 * x");

    if (formula_collection_add(collection, &f1) != 0 ||
        formula_collection_add(collection, &f2) != 0 ||
        formula_collection_add(collection, &f3) != 0) {
        printf("FAIL: unable to add formulas\n");
        formula_collection_destroy(collection);
        return 1;
    }

    if (collection->count != 3) {
        printf("FAIL: unexpected formula count after add: %zu\n", collection->count);
        formula_collection_destroy(collection);
        return 1;
    }

    formula_collection_remove(collection, "id-2");

    if (collection->count != 2) {
        printf("FAIL: unexpected formula count after remove: %zu\n", collection->count);
        formula_collection_destroy(collection);
        return 1;
    }

    if (strcmp(collection->formulas[0].id, "id-1") != 0 ||
        strcmp(collection->formulas[1].id, "id-3") != 0) {
        printf("FAIL: formulas not shifted correctly after remove: [%s, %s]\n",
               collection->formulas[0].id,
               collection->formulas[1].id);
        formula_collection_destroy(collection);
        return 1;
    }

    formula_collection_destroy(collection);
    return 0;
}

int main(void) {
    if (test_formula_collection_remove() != 0) {
        return 1;
    }

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
