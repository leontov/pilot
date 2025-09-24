/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_FKV_FKV_H
#define KOLIBRI_FKV_FKV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FKV_ENTRY_TYPE_VALUE = 0,
    FKV_ENTRY_TYPE_PROGRAM = 1,
} fkv_entry_type_t;

typedef struct {
    const uint8_t *key;
    size_t key_len;
    const uint8_t *value;
    size_t value_len;
    fkv_entry_type_t type;
    double score;
} fkv_entry_t;

typedef struct {
    fkv_entry_t *entries;
    size_t count;
} fkv_iter_t;

int fkv_init(size_t top_k_limit);
void fkv_shutdown(void);
size_t fkv_get_top_k_limit(void);
int fkv_put(const uint8_t *key,
            size_t kn,
            const uint8_t *val,
            size_t vn,
            fkv_entry_type_t type,
            double score);
int fkv_get_prefix(const uint8_t *key, size_t kn, fkv_iter_t *it, size_t k);
void fkv_iter_free(fkv_iter_t *it);
int fkv_save(const char *path);
int fkv_load(const char *path);

#ifdef __cplusplus
}
#endif

#endif
