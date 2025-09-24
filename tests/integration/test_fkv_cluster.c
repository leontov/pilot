#include "fkv/fkv.h"
#include "fkv/persistence.h"
#include "fkv/replication.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char *mkdtemp(char *);
static void insert_sample(const char *key_str, const char *val_str, fkv_entry_type_t type) {
    size_t klen = strlen(key_str);
    size_t vlen = strlen(val_str);
    uint8_t *kbuf = malloc(klen);
    uint8_t *vbuf = malloc(vlen);
    assert(kbuf && vbuf);
    for (size_t i = 0; i < klen; ++i) {
        kbuf[i] = (uint8_t)(key_str[i] - '0');
    }
    for (size_t i = 0; i < vlen; ++i) {
        vbuf[i] = (uint8_t)(val_str[i] - '0');
    }
    assert(fkv_put(kbuf, klen, vbuf, vlen, type) == 0);
    free(kbuf);
    free(vbuf);
}

static void create_dir(char *buffer, size_t size, const char *tag) {
    snprintf(buffer, size, "/tmp/%s_%ld_XXXXXX", tag, (long)getpid());
    assert(mkdtemp(buffer) != NULL);
}

static void setup_config(const char *root_dir,
                         char *wal_path,
                         size_t wal_size,
                         char *snapshot_dir,
                         size_t snapshot_size,
                         size_t interval) {
    snprintf(wal_path, wal_size, "%s/wal.log", root_dir);
    snprintf(snapshot_dir, snapshot_size, "%s/snapshots", root_dir);
    if (mkdir(snapshot_dir, 0700) != 0 && errno != EEXIST) {
        assert(0 && "mkdir failed");
    }
    fkv_persistence_config_t cfg = {
        .wal_path = wal_path,
        .snapshot_dir = snapshot_dir,
        .snapshot_interval = interval,
    };
    assert(fkv_persistence_configure(&cfg) == 0);
}

static void cleanup_paths(const char *wal_path, const char *snapshot_dir, const char *root_dir) {
    char delta_path[1024];
    size_t suffix_needed = strlen("/delta_000000000000.fkz");
    for (int i = 0; i < 4; ++i) {
        size_t needed = strlen(snapshot_dir) + suffix_needed;
        assert(needed < sizeof(delta_path));
        snprintf(delta_path, sizeof(delta_path), "%s/delta_%012d.fkz", snapshot_dir, i);
        unlink(delta_path);
    }
    unlink(wal_path);
    char base_path[1024];
    snprintf(base_path, sizeof(base_path), "%s/base.fkz", snapshot_dir);
    unlink(base_path);
    rmdir(snapshot_dir);
    rmdir(root_dir);
}

static int has_entry(const fkv_iter_t *it, const char *key_str, const char *val_str) {
    size_t klen = strlen(key_str);
    size_t vlen = strlen(val_str);
    for (size_t i = 0; i < it->count; ++i) {
        const fkv_entry_t *entry = &it->entries[i];
        if (entry->key_len != klen || entry->value_len != vlen) {
            continue;
        }
        size_t matched = 1;
        for (size_t j = 0; j < klen; ++j) {
            if (entry->key[j] != (uint8_t)(key_str[j] - '0')) {
                matched = 0;
                break;
            }
        }
        if (!matched) {
            continue;
        }
        for (size_t j = 0; j < vlen; ++j) {
            if (entry->value[j] != (uint8_t)(val_str[j] - '0')) {
                matched = 0;
                break;
            }
        }
        if (matched) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    char root_a[512];
    char root_b[512];
    create_dir(root_a, sizeof(root_a), "fkv_cluster_a");
    create_dir(root_b, sizeof(root_b), "fkv_cluster_b");

    char wal_a[1024];
    char snap_a[1024];
    setup_config(root_a, wal_a, sizeof(wal_a), snap_a, sizeof(snap_a), 2);

    assert(fkv_init() == 0);
    insert_sample("200", "01", FKV_ENTRY_TYPE_VALUE);
    insert_sample("201", "02", FKV_ENTRY_TYPE_VALUE);
    insert_sample("990", "77", FKV_ENTRY_TYPE_PROGRAM);

    const char *base_a = fkv_persistence_base_snapshot_path();
    char base_a_copy[512];
    snprintf(base_a_copy, sizeof(base_a_copy), "%s", base_a);
    assert(fkv_save(base_a_copy) == 0);
    assert(fkv_persistence_force_checkpoint() == 0);

    SwarmFrame delta_ab = {0};
    assert(fkv_replication_build_delta(NULL, 0, &delta_ab) == 0);

    fkv_shutdown();
    fkv_persistence_disable();

    char wal_b[1024];
    char snap_b[1024];
    setup_config(root_b, wal_b, sizeof(wal_b), snap_b, sizeof(snap_b), 2);
    assert(fkv_init() == 0);
    assert(fkv_replication_apply_delta(&delta_ab) == 0);
    fkv_replication_free_delta(&delta_ab);

    const char *base_b = fkv_persistence_base_snapshot_path();
    char base_b_copy[512];
    snprintf(base_b_copy, sizeof(base_b_copy), "%s", base_b);
    assert(fkv_save(base_b_copy) == 0);
    assert(fkv_persistence_force_checkpoint() == 0);

    insert_sample("202", "03", FKV_ENTRY_TYPE_VALUE);
    SwarmFrame delta_ba = {0};
    assert(fkv_replication_build_delta(NULL, 0, &delta_ba) == 0);

    fkv_shutdown();
    fkv_persistence_disable();

    setup_config(root_a, wal_a, sizeof(wal_a), snap_a, sizeof(snap_a), 2);
    assert(fkv_init() == 0);
    assert(fkv_replication_apply_delta(&delta_ba) == 0);
    fkv_replication_free_delta(&delta_ba);

    uint8_t prefix2[] = {2, 0};
    fkv_iter_t it = {0};
    assert(fkv_get_prefix(prefix2, sizeof(prefix2), &it, 0) == 0);
    assert(has_entry(&it, "200", "01"));
    assert(has_entry(&it, "202", "03"));
    fkv_iter_free(&it);

    fkv_shutdown();
    fkv_persistence_disable();

    cleanup_paths(wal_a, snap_a, root_a);
    cleanup_paths(wal_b, snap_b, root_b);

    printf("cluster replication test passed\n");
    return 0;
}
