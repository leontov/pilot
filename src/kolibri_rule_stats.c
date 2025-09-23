#include "kolibri_rule_stats.h"

void update_rule_stats(rule_stats_t* stats, bool success, double response_time) {
    stats->total_uses++;
    if (success) stats->successful_uses++;
    // Экспоненциальное скользящее среднее
    double alpha = 0.1;
    if (stats->avg_response_time == 0)
        stats->avg_response_time = response_time;
    else
        stats->avg_response_time = alpha * response_time + (1 - alpha) * stats->avg_response_time;
    // Уверенность
    stats->confidence = (stats->total_uses > 0) ? ((double)stats->successful_uses / stats->total_uses) : 0.0;
}

void adjust_fitness(double* fitness, const rule_stats_t* stats) {
    // Простая формула: fitness стремится к confidence
    if (!fitness || !stats) return;
    double delta = stats->confidence - *fitness;
    *fitness += 0.05 * delta;
}
