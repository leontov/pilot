#ifndef KOLIBRI_FKV_FKV_H
#define KOLIBRI_FKV_FKV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *key;
    size_t key_len;
    const uint8_t *value;
    size_t value_len;
} fkv_entry_t;

typedef struct {
    fkv_entry_t *entries;
    size_t count;
} fkv_iter_t;

int fkv_init(void);
void fkv_shutdown(void);
int fkv_put(const uint8_t *key, size_t kn, const uint8_t *val, size_t vn);
int fkv_get_prefix(const uint8_t *key, size_t kn, fkv_iter_t *it, size_t k);
void fkv_iter_free(fkv_iter_t *it);

#ifdef __cplusplus
}
#endif

#endif
