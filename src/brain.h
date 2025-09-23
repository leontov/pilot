#ifndef BRAIN_H
#define BRAIN_H

#include <stddef.h>

// Initialize brain (loads weights or creates defaults)
int brain_init(const char *weights_file);

// Predict effectiveness in [0,1] from features
double brain_predict(const double *features, size_t n_features);

// Update weights with target (0..1), simple SGD
void brain_update(const double *features, size_t n_features, double target, double lr);

// Save weights
int brain_save(const char *weights_file);

#endif // BRAIN_H
