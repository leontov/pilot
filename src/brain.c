#include "brain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static double *weights = NULL;
static size_t n_weights = 0;

static double sigmoid(double x) { return 1.0 / (1.0 + exp(-x)); }

int brain_init(const char *weights_file) {
    // For simplicity we use fixed 3 features: complexity, length, recent_score
    n_weights = 4; // 3 + bias
    weights = calloc(n_weights, sizeof(double));
    if (!weights) return -1;
    // Try to load
    FILE *f = fopen(weights_file, "r");
    if (f) {
        for (size_t i = 0; i < n_weights; ++i) fscanf(f, "%lf", &weights[i]);
        fclose(f);
    } else {
        // init small random
        for (size_t i = 0; i < n_weights; ++i) weights[i] = ((double)rand() / RAND_MAX - 0.5) * 0.1;
    }
    return 0;
}

double brain_predict(const double *features, size_t n_features) {
    if (!weights || n_features + 1 != n_weights) return 0.5;
    double sum = weights[0]; // bias
    for (size_t i = 0; i < n_features; ++i) sum += weights[i+1] * features[i];
    return sigmoid(sum);
}

void brain_update(const double *features, size_t n_features, double target, double lr) {
    if (!weights || n_features + 1 != n_weights) return;
    double pred = brain_predict(features, n_features);
    double err = target - pred;
    weights[0] += lr * err; // bias
    for (size_t i = 0; i < n_features; ++i) weights[i+1] += lr * err * features[i];
}

int brain_save(const char *weights_file) {
    if (!weights) return -1;
    FILE *f = fopen(weights_file, "w");
    if (!f) return -1;
    for (size_t i = 0; i < n_weights; ++i) fprintf(f, "%lf\n", weights[i]);
    fclose(f);
    return 0;
}
