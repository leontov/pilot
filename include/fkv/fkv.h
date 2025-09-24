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
    uint64_t priority;
} fkv_entry_t;

typedef struct {
    fkv_entry_t *entries;
    size_t count;
} fkv_iter_t;

typedef struct {
    uint8_t *key;
    size_t key_len;
    uint8_t *value;
    size_t value_len;
    fkv_entry_type_t type;
    uint64_t priority;
} fkv_delta_entry_t;

typedef struct {
    fkv_delta_entry_t *entries;
    size_t count;
    size_t capacity;
    uint64_t min_sequence;
    uint64_t max_sequence;
    size_t total_bytes;
    uint16_t checksum;
} fkv_delta_t;

int fkv_init(void);
void fkv_shutdown(void);
int fkv_put(const uint8_t *key, size_t kn, const uint8_t *val, size_t vn, fkv_entry_type_t type);
int fkv_put_scored(const uint8_t *key,
                   size_t kn,
                   const uint8_t *val,
                   size_t vn,
                   fkv_entry_type_t type,
                   uint64_t priority);
int fkv_get_prefix(const uint8_t *key, size_t kn, fkv_iter_t *it, size_t k);
void fkv_iter_free(fkv_iter_t *it);
void fkv_set_topk_limit(size_t limit);
size_t fkv_get_topk_limit(void);
int fkv_save(const char *path);
int fkv_load(const char *path);
uint64_t fkv_current_sequence(void);
int fkv_export_delta(uint64_t since_sequence, fkv_delta_t *delta);
int fkv_apply_delta(const fkv_delta_t *delta);
void fkv_delta_free(fkv_delta_t *delta);
uint16_t fkv_delta_compute_checksum(const fkv_delta_t *delta);

#ifdef __cplusplus
}
#endif

#endif
