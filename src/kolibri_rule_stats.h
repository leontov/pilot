#ifndef KOLIBRI_RULE_STATS_H
#define KOLIBRI_RULE_STATS_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_RULES 1000

typedef struct {
    uint64_t total_uses;
    uint64_t successful_uses;
    double avg_response_time;
    double confidence;
} rule_stats_t;

void update_rule_stats(rule_stats_t* stats, bool success, double response_time);
void adjust_fitness(double* fitness, const rule_stats_t* stats);

#endif
