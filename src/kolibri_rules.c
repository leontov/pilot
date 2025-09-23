#include "kolibri_rules.h"
#include <stdlib.h>
#include <string.h>

int init_rules(rules_t* rules) {
    rules->count = 0;
    return 0;
}

int add_rule(rules_t* rules, const char* pattern, const char* action,
             int tier, double fitness) {
    if (rules->count >= MAX_RULES) {
        return -1;
    }

    int idx = rules->count++;
    strncpy(rules->patterns[idx], pattern, MAX_PATTERN_LEN - 1);
    strncpy(rules->actions[idx], action, MAX_ACTION_LEN - 1);
    rules->tiers[idx] = tier;
    rules->fitness[idx] = fitness;

    return 0;
}

int find_rule(rules_t* rules, const char* pattern, char* action,
              size_t action_size) {
    for (int i = 0; i < rules->count; i++) {
        if (strcmp(rules->patterns[i], pattern) == 0) {
            strncpy(action, rules->actions[i], action_size - 1);
            action[action_size - 1] = '\0';
            return rules->tiers[i];
        }
    }
    return -1;
}

void cleanup_rules(rules_t* rules) {
    if (rules) {
        // Очищаем все правила
        memset(rules->patterns, 0, sizeof(rules->patterns));
        memset(rules->actions, 0, sizeof(rules->actions));
        memset(rules->tiers, 0, sizeof(rules->tiers));
        memset(rules->fitness, 0, sizeof(rules->fitness));
        rules->count = 0;
    }
}
