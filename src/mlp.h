#ifndef MLP_H
#define MLP_H

#include <stddef.h>

int mlp_init(size_t in_dim, size_t hidden, size_t out_dim);
double mlp_predict(const double* x); // returns scalar
void mlp_update(const double* x, double target, double lr);
int mlp_save(const char* path);
int mlp_load(const char* path);
void mlp_free(void);

#endif // MLP_H
