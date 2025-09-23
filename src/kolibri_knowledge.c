#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include "kolibri_knowledge.h"
#include "kolibri_rules.h"

int load_base_knowledge(const char* filename, const char* specialization, rules_t* rules) {
    struct json_object *root_obj, *spec_obj, *rule_array;
    
    // Load and parse JSON file
    root_obj = json_object_from_file(filename);
    if (!root_obj) {
        fprintf(stderr, "Error loading knowledge file: %s\n", filename);
        return -1;
    }

    // Get specialization array
    if (!json_object_object_get_ex(root_obj, specialization, &spec_obj)) {
        fprintf(stderr, "Specialization '%s' not found in knowledge base\n", specialization);
        json_object_put(root_obj);
        return -1;
    }

    if (json_object_get_type(spec_obj) != json_type_array) {
        fprintf(stderr, "Invalid knowledge format for specialization '%s'\n", specialization);
        json_object_put(root_obj);
        return -1;
    }

    rule_array = spec_obj;
    int array_len = json_object_array_length(rule_array);
    
    // Initialize rules from JSON array
    for (int i = 0; i < array_len; i++) {
        struct json_object *rule_obj = json_object_array_get_idx(rule_array, i);
        if (init_rules_from_json(rule_obj, rules) != 0) {
            fprintf(stderr, "Error loading rule %d\n", i);
            json_object_put(root_obj);
            return -1;
        }
    }

    json_object_put(root_obj);
    return 0;
}

int init_rules_from_json(struct json_object* rule_obj, rules_t* rules) {
    struct json_object *pattern_obj, *action_obj, *tier_obj, *fitness_obj;
    
    if (!json_object_object_get_ex(rule_obj, "pattern", &pattern_obj) ||
        !json_object_object_get_ex(rule_obj, "action", &action_obj) ||
        !json_object_object_get_ex(rule_obj, "tier", &tier_obj) ||
        !json_object_object_get_ex(rule_obj, "fitness", &fitness_obj)) {
        return -1;
    }

    const char* pattern = json_object_get_string(pattern_obj);
    const char* action = json_object_get_string(action_obj);
    int tier = json_object_get_int(tier_obj);
    double fitness = json_object_get_double(fitness_obj);

    // Add rule to the rules storage
    return add_rule(rules, pattern, action, tier, fitness);
}
