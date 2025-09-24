/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "fkv/fkv.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void insert_sample(const char *key_str,
                          const char *val_str,
                          fkv_entry_type_t type,
                          double score) {
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
    assert(fkv_put(kbuf, klen, vbuf, vlen, type, score) == 0);
    free(kbuf);
    free(vbuf);
}

static void assert_key_equals(const fkv_entry_t *entry, const char *expected) {
    size_t len = strlen(expected);
    assert(entry->key_len == len);
    for (size_t i = 0; i < len; ++i) {
        assert(entry->key[i] == (uint8_t)(expected[i] - '0'));
    }
}

static void create_temp_snapshot(char *buffer, size_t buffer_size, const char *tag) {
    static unsigned counter = 0;
    snprintf(buffer, buffer_size, "/tmp/%s_%ld_%u.snapshot", tag, (long)getpid(), counter++);
    int fd = open(buffer, O_CREAT | O_TRUNC | O_RDWR, 0600);
    assert(fd != -1);
    close(fd);
}

static void test_prefix(void) {
    fkv_init(4);
    insert_sample("123", "45", FKV_ENTRY_TYPE_VALUE, 0.5);
    insert_sample("124", "67", FKV_ENTRY_TYPE_VALUE, 0.75);
    insert_sample("129", "89", FKV_ENTRY_TYPE_VALUE, 0.25);
    insert_sample("880", "987654", FKV_ENTRY_TYPE_PROGRAM, 0.9);

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
    fkv_init(3);
    insert_sample("123", "45", FKV_ENTRY_TYPE_VALUE, 1.25);
    insert_sample("555", "99", FKV_ENTRY_TYPE_VALUE, 2.5);

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
    assert(it.entries[0].score == 1.25);
    fkv_iter_free(&it);

    uint8_t key555[] = {5, 5, 5};
    assert(fkv_get_prefix(key555, sizeof(key555), &it, 1) == 0);
    assert(it.count == 1);
    assert(memcmp(it.entries[0].value, (uint8_t[]){9, 9}, 2) == 0);
    assert(it.entries[0].score == 2.5);
    fkv_iter_free(&it);

    fkv_shutdown();

    unlink(snapshot);
}

static void test_load_overwrites_existing(void) {
    fkv_init(2);
    insert_sample("123", "45", FKV_ENTRY_TYPE_VALUE, 0.4);

    char snapshot[128];
    create_temp_snapshot(snapshot, sizeof(snapshot), "fkv_snapshot_overwrite");
    assert(fkv_save(snapshot) == 0);
    fkv_shutdown();

    fkv_init(2);
    insert_sample("999", "11", FKV_ENTRY_TYPE_VALUE, 0.7);
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

static void test_topk_enforced(void) {
    fkv_init(3);
    insert_sample("120", "01", FKV_ENTRY_TYPE_VALUE, 0.4);
    insert_sample("121", "02", FKV_ENTRY_TYPE_VALUE, 0.9);
    insert_sample("122", "03", FKV_ENTRY_TYPE_VALUE, 0.7);
    insert_sample("123", "04", FKV_ENTRY_TYPE_VALUE, 0.6);

    uint8_t prefix[] = {1, 2};
    fkv_iter_t it = {0};
    assert(fkv_get_prefix(prefix, sizeof(prefix), &it, 0) == 0);
    assert(it.count == 3);
    assert_key_equals(&it.entries[0], "121");
    assert(it.entries[0].score == 0.9);
    assert_key_equals(&it.entries[1], "122");
    assert(it.entries[1].score == 0.7);
    assert_key_equals(&it.entries[2], "123");
    assert(it.entries[2].score == 0.6);
    fkv_iter_free(&it);

    insert_sample("123", "14", FKV_ENTRY_TYPE_VALUE, 0.95);
    assert(fkv_get_prefix(prefix, sizeof(prefix), &it, 0) == 0);
    assert(it.count == 3);
    assert_key_equals(&it.entries[0], "123");
    assert(it.entries[0].score == 0.95);
    assert_key_equals(&it.entries[1], "121");
    assert(it.entries[1].score == 0.9);
    assert_key_equals(&it.entries[2], "122");
    assert(it.entries[2].score == 0.7);
    fkv_iter_free(&it);

    fkv_shutdown();
}

int main(void) {
    test_prefix();
    test_serialization_roundtrip();
    test_load_overwrites_existing();
    test_topk_enforced();
    printf("fkv tests passed\n");
    return 0;
}
