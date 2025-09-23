#ifndef RULE_ENGINE_H
#define RULE_ENGINE_H

#include <json-c/json.h>

typedef struct {
    char storage_prefix[256];
} RuleEngine;

// Initialize rule engine with storage prefix (e.g., node_<port>)
int rule_engine_init(RuleEngine *re, const char *storage_prefix);
// Free resources
void rule_engine_free(RuleEngine *re);
// Add rule (json object will be copied)
int rule_engine_add(RuleEngine *re, json_object *rule);
// Get rules as json array (caller should json_object_put the returned obj)
json_object* rule_engine_list(RuleEngine *re);
// Save rules (force flush)
int rule_engine_save(RuleEngine *re);
// Find best matching rule for a task (returns a new ref or NULL)
json_object* rule_engine_find_best_match(RuleEngine *re, const char *task);
// Record that a rule was used (increment hits and recompute score)
int rule_engine_record_hit(RuleEngine *re, const char *rule_id);
// Record a successful application of a rule (increment successes and recompute score)
int rule_engine_record_success(RuleEngine *re, const char *rule_id);
// Run garbage-collection/pruning on rules; removes rules with low success rate
int rule_engine_gc(RuleEngine *re, int min_hits, double min_success_rate);
// Merge rules from a cluster-wide JSON file (adds missing rules)
int rule_engine_sync(RuleEngine *re, const char *cluster_path);

#endif // RULE_ENGINE_H
