#ifndef KOLIBRI_RULES_H
#define KOLIBRI_RULES_H

#include <stddef.h>

#define MAX_RULES 1000
#define MAX_PATTERN_LEN 256
#define MAX_ACTION_LEN 256

typedef struct {
    char patterns[MAX_RULES][MAX_PATTERN_LEN];
    char actions[MAX_RULES][MAX_ACTION_LEN];
    int tiers[MAX_RULES];
    double fitness[MAX_RULES];
    int count;
} rules_t;

int init_rules(rules_t* rules);
int add_rule(rules_t* rules, const char* pattern, const char* action, int tier, double fitness);
int find_rule(rules_t* rules, const char* pattern, char* action, size_t action_size);
void cleanup_rules(rules_t* rules);

#endif
