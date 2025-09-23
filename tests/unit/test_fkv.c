#include "fkv/fkv.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(void) {
    test_prefix();
    printf("fkv tests passed\n");
    return 0;
}
