#ifndef KOLIBRI_FKV_PERSISTENCE_H
#define KOLIBRI_FKV_PERSISTENCE_H

#include "fkv/fkv.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *wal_path;
    const char *snapshot_dir;
    size_t snapshot_interval;
} fkv_persistence_config_t;

typedef int (*fkv_persistence_apply_fn)(const uint8_t *key,
                                        size_t key_len,
                                        const uint8_t *value,
                                        size_t value_len,
                                        fkv_entry_type_t type,
                                        void *userdata);

int fkv_persistence_configure(const fkv_persistence_config_t *config);
void fkv_persistence_disable(void);
bool fkv_persistence_enabled(void);

int fkv_persistence_start(fkv_persistence_apply_fn apply, void *userdata);
int fkv_persistence_record_put(const uint8_t *key,
                               size_t key_len,
                               const uint8_t *value,
                               size_t value_len,
                               fkv_entry_type_t type);
int fkv_persistence_force_checkpoint(void);
void fkv_persistence_shutdown(void);

const char *fkv_persistence_wal_path(void);
const char *fkv_persistence_base_snapshot_path(void);

#ifdef __cplusplus
}
#endif

#endif
