/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "fkv/fkv.h"
#include "fkv/persistence.h"
#include "fkv/replication.h"
#include "protocol/swarm.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

extern char *mkdtemp(char *);

static void insert_sample(const char *key_str, const char *val_str, fkv_entry_type_t type) {
    size_t klen = strlen(key_str);
    size_t vlen = strlen(val_str);
    uint8_t *kbuf = malloc(klen);
    uint8_t *vbuf = malloc(vlen);
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

static int entry_matches(const fkv_entry_t *entry, const char *key_str, const char *val_str) {
    size_t klen = strlen(key_str);
    size_t vlen = strlen(val_str);
    if (entry->key_len != klen || entry->value_len != vlen) {
        return 0;
    }
    for (size_t i = 0; i < klen; ++i) {
        if (entry->key[i] != (uint8_t)(key_str[i] - '0')) {
            return 0;
        }
    }
    for (size_t i = 0; i < vlen; ++i) {
        if (entry->value[i] != (uint8_t)(val_str[i] - '0')) {
            return 0;
        }
    }
    return 1;
}

static void create_temp_snapshot(char *buffer, size_t buffer_size, const char *tag) {
    static unsigned counter = 0;
    snprintf(buffer, buffer_size, "/tmp/%s_%ld_%u.snapshot", tag, (long)getpid(), counter++);
    int fd = open(buffer, O_CREAT | O_TRUNC | O_RDWR, 0600);
    assert(fd != -1);
    close(fd);
}

static void create_temp_dir(char *buffer, size_t buffer_size, const char *tag) {
    snprintf(buffer, buffer_size, "/tmp/%s_%ld_XXXXXX", tag, (long)getpid());
    assert(mkdtemp(buffer) != NULL);
}

static void test_prefix(void) {
    fkv_init();
    insert_sample("123", "45", FKV_ENTRY_TYPE_VALUE);
    insert_sample("124", "67", FKV_ENTRY_TYPE_VALUE);
    insert_sample("129", "89", FKV_ENTRY_TYPE_VALUE);
    insert_sample("880", "987654", FKV_ENTRY_TYPE_PROGRAM);

    uint8_t prefix[] = {1, 2};
    fkv_iter_t it = {0};
    assert(fkv_get_prefix(prefix, sizeof(prefix), &it, 3) == 0);
    assert(it.count >= 2);
    for (size_t i = 0; i < it.count; ++i) {
        assert(it.entries[i].key_len >= 2);
        assert(it.entries[i].type == FKV_ENTRY_TYPE_VALUE);
    }
    fkv_iter_free(&it);

    uint8_t program_prefix[] = {8, 8};
    assert(fkv_get_prefix(program_prefix, sizeof(program_prefix), &it, 2) == 0);
    assert(it.count == 1);
    assert(it.entries[0].type == FKV_ENTRY_TYPE_PROGRAM);
    fkv_iter_free(&it);
    fkv_shutdown();
}

static void test_serialization_roundtrip(void) {
    fkv_init();
    insert_sample("123", "45", FKV_ENTRY_TYPE_VALUE);
    insert_sample("555", "99", FKV_ENTRY_TYPE_VALUE);

    char snapshot[128];
    create_temp_snapshot(snapshot, sizeof(snapshot), "fkv_snapshot_roundtrip");

    assert(fkv_save(snapshot) == 0);
    fkv_shutdown();

    assert(fkv_load(snapshot) == 0);

    uint8_t key123[] = {1, 2, 3};
    fkv_iter_t it = {0};
    assert(fkv_get_prefix(key123, sizeof(key123), &it, 1) == 0);
    assert(it.count == 1);
    assert(it.entries[0].value_len == 2);
    assert(memcmp(it.entries[0].value, (uint8_t[]){4, 5}, 2) == 0);
    fkv_iter_free(&it);

    uint8_t key555[] = {5, 5, 5};
    assert(fkv_get_prefix(key555, sizeof(key555), &it, 1) == 0);
    assert(it.count == 1);
    assert(memcmp(it.entries[0].value, (uint8_t[]){9, 9}, 2) == 0);
    fkv_iter_free(&it);

    fkv_shutdown();

    unlink(snapshot);
}

static void test_load_overwrites_existing(void) {
    fkv_init();
    insert_sample("123", "45", FKV_ENTRY_TYPE_VALUE);

    char snapshot[128];
    create_temp_snapshot(snapshot, sizeof(snapshot), "fkv_snapshot_overwrite");
    assert(fkv_save(snapshot) == 0);
    fkv_shutdown();

    fkv_init();
    insert_sample("999", "11", FKV_ENTRY_TYPE_VALUE);
    assert(fkv_load(snapshot) == 0);

    uint8_t key999[] = {9, 9, 9};
    fkv_iter_t it = {0};
    assert(fkv_get_prefix(key999, sizeof(key999), &it, 1) == 0);
    assert(it.count == 0);

    uint8_t key123[] = {1, 2, 3};
    assert(fkv_get_prefix(key123, sizeof(key123), &it, 1) == 0);
    assert(it.count == 1);
    fkv_iter_free(&it);

    fkv_shutdown();
    unlink(snapshot);
}

static void test_persistence_recovery(void) {
    char root_dir[512];
    create_temp_dir(root_dir, sizeof(root_dir), "fkv_persist_root");

    char wal_path[1024];
    char snapshot_dir[1024];
    snprintf(wal_path, sizeof(wal_path), "%s/wal.log", root_dir);
    snprintf(snapshot_dir, sizeof(snapshot_dir), "%s/snapshots", root_dir);
    assert(mkdir(snapshot_dir, 0700) == 0);

    fkv_persistence_config_t cfg = {
        .wal_path = wal_path,
        .snapshot_dir = snapshot_dir,
        .snapshot_interval = 2,
    };
    assert(fkv_persistence_configure(&cfg) == 0);

    assert(fkv_init() == 0);
    insert_sample("120", "01", FKV_ENTRY_TYPE_VALUE);
    insert_sample("121", "02", FKV_ENTRY_TYPE_VALUE);
    insert_sample("980", "777", FKV_ENTRY_TYPE_PROGRAM);

    const char *base_path = fkv_persistence_base_snapshot_path();
    assert(base_path);
    char base_copy[512];
    snprintf(base_copy, sizeof(base_copy), "%s", base_path);
    assert(fkv_save(base_copy) == 0);
    assert(fkv_persistence_force_checkpoint() == 0);
    fkv_shutdown();

    assert(fkv_init() == 0);
    uint8_t prefix12[] = {1, 2};
    fkv_iter_t it = {0};
    assert(fkv_get_prefix(prefix12, sizeof(prefix12), &it, 0) == 0);
    assert(it.count >= 2);
    int found120 = 0;
    for (size_t i = 0; i < it.count; ++i) {
        if (entry_matches(&it.entries[i], "120", "01")) {
            found120 = 1;
        }
    }
    assert(found120);
    fkv_iter_free(&it);

    uint8_t prefix98[] = {9, 8};
    assert(fkv_get_prefix(prefix98, sizeof(prefix98), &it, 0) == 0);
    assert(it.count == 1);
    assert(it.entries[0].type == FKV_ENTRY_TYPE_PROGRAM);
    fkv_iter_free(&it);

    fkv_shutdown();
    fkv_persistence_disable();

    unlink(base_copy);
    char delta_path[1024];
    size_t required = strlen(snapshot_dir) + strlen("/delta_000000000000.fkz");
    assert(required < sizeof(delta_path));
    snprintf(delta_path, sizeof(delta_path), "%s/delta_%012d.fkz", snapshot_dir, 0);
    unlink(delta_path);
    unlink(wal_path);
    rmdir(snapshot_dir);
    rmdir(root_dir);
}

static void test_replication_delta_flow(void) {
    assert(fkv_init() == 0);
    insert_sample("120", "01", FKV_ENTRY_TYPE_VALUE);
    insert_sample("121", "02", FKV_ENTRY_TYPE_VALUE);
    insert_sample("129", "33", FKV_ENTRY_TYPE_PROGRAM);

    uint8_t prefix[] = {1, 2};
    SwarmFrame frame = {0};
    assert(fkv_replication_build_delta(prefix, sizeof(prefix), &frame) == 0);
    assert(frame.payload.fkv_delta.entry_count >= 2);

    fkv_shutdown();
    assert(fkv_init() == 0);
    insert_sample("120", "99", FKV_ENTRY_TYPE_VALUE);

    assert(fkv_replication_apply_delta(&frame) == 0);
    fkv_replication_free_delta(&frame);

    fkv_iter_t it = {0};
    assert(fkv_get_prefix(prefix, sizeof(prefix), &it, 0) == 0);
    int restored = 0;
    for (size_t i = 0; i < it.count; ++i) {
        if (entry_matches(&it.entries[i], "120", "01")) {
            restored = 1;
        }
    }
    assert(restored);
    fkv_iter_free(&it);
    fkv_shutdown();
}

int main(void) {
    test_prefix();
    test_serialization_roundtrip();
    test_load_overwrites_existing();
    test_persistence_recovery();
    test_replication_delta_flow();
    printf("fkv tests passed\n");
    return 0;
}
