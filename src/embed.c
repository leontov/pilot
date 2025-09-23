#include "embed.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static size_t g_dim = 0;

int embed_init(size_t dim) {
    if (dim == 0) return -1;
    g_dim = dim;
    return 0;
}

// Простая хеш-функция (32-bit)
static uint32_t hash32(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

// Заполняем out[dim] малыми значениями, суммируем n-граммы
void embed_text(const char* text, double* out, size_t dim) {
    if (!text || !out || dim == 0) return;
    // Ноль
    for (size_t i = 0; i < dim; ++i) out[i] = 0.0;

    size_t len = strlen(text);
    // Простые n-граммы байтовые: 1..3-grams
    for (size_t i = 0; i < len; ++i) {
        for (int n = 1; n <= 3 && i + n <= len; ++n) {
            char tmp[4] = {0,0,0,0};
            for (int k = 0; k < n; ++k) tmp[k] = text[i+k];
            uint32_t h = hash32(tmp);
            size_t idx = (size_t)(h % dim);
            double val = ((h >> 16) & 0xFFFF) / (double)0xFFFF;
            out[idx] += val;
        }
    }

    // Normalize L2
    double norm = 0.0;
    for (size_t i = 0; i < dim; ++i) norm += out[i]*out[i];
    if (norm <= 0.0) return;
    norm = sqrt(norm);
    for (size_t i = 0; i < dim; ++i) out[i] /= norm;
}

void embed_free(void) {
    g_dim = 0;
}
