#ifndef KOLIBRI_KNOWLEDGE_H
#define KOLIBRI_KNOWLEDGE_H

#include <json-c/json.h>
#include "kolibri_rules.h"

// Load base knowledge from JSON file
int load_base_knowledge(const char* filename, const char* specialization, rules_t* rules);

// Initialize rules from JSON object
int init_rules_from_json(struct json_object* rule_obj, rules_t* rules);

#endif
