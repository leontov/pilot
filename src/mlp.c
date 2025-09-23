#include "mlp.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

static size_t in_dim_g = 0;
static size_t hidden_g = 0;
static size_t out_dim_g = 0;
static double *w1 = NULL; // in_dim * hidden
static double *b1 = NULL; // hidden
static double *w2 = NULL; // hidden * out_dim (out_dim==1 assumed)
static double *b2 = NULL; // out_dim

// Adam optimizer state (for simplicity, per-weight m/v arrays)
static double *m_w1 = NULL, *v_w1 = NULL;
static double *m_b1 = NULL, *v_b1 = NULL;
static double *m_w2 = NULL, *v_w2 = NULL;
static double *m_b2 = NULL, *v_b2 = NULL;
static size_t adam_step = 0;

int mlp_init(size_t in_dim, size_t hidden, size_t out_dim) {
    if (in_dim == 0 || hidden == 0 || out_dim == 0) return -1;
    in_dim_g = in_dim; hidden_g = hidden; out_dim_g = out_dim;
    w1 = calloc(in_dim * hidden, sizeof(double));
    b1 = calloc(hidden, sizeof(double));
    w2 = calloc(hidden * out_dim, sizeof(double));
    b2 = calloc(out_dim, sizeof(double));
    // Adam state alloc
    m_w1 = calloc(in_dim * hidden, sizeof(double)); v_w1 = calloc(in_dim * hidden, sizeof(double));
    m_b1 = calloc(hidden, sizeof(double)); v_b1 = calloc(hidden, sizeof(double));
    m_w2 = calloc(hidden * out_dim, sizeof(double)); v_w2 = calloc(hidden * out_dim, sizeof(double));
    m_b2 = calloc(out_dim, sizeof(double)); v_b2 = calloc(out_dim, sizeof(double));
    if (!w1 || !b1 || !w2 || !b2) return -1;
    // small random init
    for (size_t i = 0; i < in_dim*hidden; ++i) w1[i] = ((double)rand()/RAND_MAX - 0.5) * 0.1;
    for (size_t i = 0; i < hidden; ++i) b1[i] = 0.0;
    for (size_t i = 0; i < hidden*out_dim; ++i) w2[i] = ((double)rand()/RAND_MAX - 0.5) * 0.1;
    for (size_t i = 0; i < out_dim; ++i) b2[i] = 0.0;
    return 0;
}

static double relu(double x) { return x > 0 ? x : 0; }
static double drelu(double x) { return x > 0 ? 1.0 : 0.0; }

double mlp_predict(const double* x) {
    if (!x || !w1) return 0.5;
    double *h = malloc(sizeof(double)*hidden_g);
    for (size_t j = 0; j < hidden_g; ++j) {
        double s = b1[j];
        for (size_t i = 0; i < in_dim_g; ++i) s += x[i] * w1[i*hidden_g + j];
        h[j] = relu(s);
    }
    double out = b2[0];
    for (size_t j = 0; j < hidden_g; ++j) out += h[j] * w2[j];
    free(h);
    // sigmoid to map to 0..1
    return 1.0 / (1.0 + exp(-out));
}

void mlp_update(const double* x, double target, double lr) {
    if (!x || !w1) return;
    // Forward
    double *h = malloc(sizeof(double)*hidden_g);
    double *s1 = malloc(sizeof(double)*hidden_g);
    for (size_t j = 0; j < hidden_g; ++j) {
        double s = b1[j];
        for (size_t i = 0; i < in_dim_g; ++i) s += x[i] * w1[i*hidden_g + j];
        s1[j] = s;
        h[j] = relu(s);
    }
    double out = b2[0];
    for (size_t j = 0; j < hidden_g; ++j) out += h[j] * w2[j];
    double pred = 1.0 / (1.0 + exp(-out));
    double err = pred - target; // dL/dout (for MSE-like)

    // Backprop with Adam-like updates
    adam_step++;
    const double beta1 = 0.9, beta2 = 0.999, eps = 1e-8;

    for (size_t j = 0; j < hidden_g; ++j) {
        double grad_w2 = err * h[j] * pred * (1.0 - pred);
        // m, v update
        m_w2[j] = beta1 * m_w2[j] + (1 - beta1) * grad_w2;
        v_w2[j] = beta2 * v_w2[j] + (1 - beta2) * (grad_w2 * grad_w2);
        double m_hat = m_w2[j] / (1 - pow(beta1, adam_step));
        double v_hat = v_w2[j] / (1 - pow(beta2, adam_step));
        w2[j] -= lr * m_hat / (sqrt(v_hat) + eps);
    }
    double grad_b2 = err * pred * (1.0 - pred);
    m_b2[0] = beta1 * m_b2[0] + (1 - beta1) * grad_b2;
    v_b2[0] = beta2 * v_b2[0] + (1 - beta2) * (grad_b2 * grad_b2);
    double m_hat_b2 = m_b2[0] / (1 - pow(beta1, adam_step));
    double v_hat_b2 = v_b2[0] / (1 - pow(beta2, adam_step));
    b2[0] -= lr * m_hat_b2 / (sqrt(v_hat_b2) + eps);

    // Hidden layer gradients
    for (size_t j = 0; j < hidden_g; ++j) {
        double dh = err * w2[j] * pred * (1.0 - pred);
        double dpre = dh * drelu(s1[j]);
        for (size_t i = 0; i < in_dim_g; ++i) {
            double g = dpre * x[i];
            size_t idx = i*hidden_g + j;
            m_w1[idx] = beta1 * m_w1[idx] + (1 - beta1) * g;
            v_w1[idx] = beta2 * v_w1[idx] + (1 - beta2) * (g * g);
            double m_h = m_w1[idx] / (1 - pow(beta1, adam_step));
            double v_h = v_w1[idx] / (1 - pow(beta2, adam_step));
            w1[idx] -= lr * m_h / (sqrt(v_h) + eps);
        }
        m_b1[j] = beta1 * m_b1[j] + (1 - beta1) * dpre;
        v_b1[j] = beta2 * v_b1[j] + (1 - beta2) * (dpre * dpre);
        double m_b_hat = m_b1[j] / (1 - pow(beta1, adam_step));
        double v_b_hat = v_b1[j] / (1 - pow(beta2, adam_step));
        b1[j] -= lr * m_b_hat / (sqrt(v_b_hat) + eps);
    }

    free(h);
    free(s1);
}

int mlp_save(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(&in_dim_g, sizeof(size_t), 1, f);
    fwrite(&hidden_g, sizeof(size_t), 1, f);
    fwrite(&out_dim_g, sizeof(size_t), 1, f);
    fwrite(w1, sizeof(double), in_dim_g*hidden_g, f);
    fwrite(b1, sizeof(double), hidden_g, f);
    fwrite(w2, sizeof(double), hidden_g*out_dim_g, f);
    fwrite(b2, sizeof(double), out_dim_g, f);
    fclose(f);
    return 0;
}

int mlp_load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    size_t ind, hid, outd;
    fread(&ind, sizeof(size_t), 1, f);
    fread(&hid, sizeof(size_t), 1, f);
    fread(&outd, sizeof(size_t), 1, f);
    if (ind != in_dim_g || hid != hidden_g || outd != out_dim_g) { fclose(f); return -1; }
    fread(w1, sizeof(double), in_dim_g*hidden_g, f);
    fread(b1, sizeof(double), hidden_g, f);
    fread(w2, sizeof(double), hidden_g*out_dim_g, f);
    fread(b2, sizeof(double), out_dim_g, f);
    fclose(f);
    return 0;
}

void mlp_free(void) {
    free(w1); w1 = NULL;
    free(b1); b1 = NULL;
    free(w2); w2 = NULL;
    free(b2); b2 = NULL;
    in_dim_g = hidden_g = out_dim_g = 0;
    free(m_w1); m_w1 = NULL; free(v_w1); v_w1 = NULL;
    free(m_b1); m_b1 = NULL; free(v_b1); v_b1 = NULL;
    free(m_w2); m_w2 = NULL; free(v_w2); v_w2 = NULL;
    free(m_b2); m_b2 = NULL; free(v_b2); v_b2 = NULL;
    adam_step = 0;
}
