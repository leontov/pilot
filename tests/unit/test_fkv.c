#include "fkv/fkv.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void insert_sample(const char *key_str, const char *val_str) {
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
    assert(fkv_put(kbuf, klen, vbuf, vlen) == 0);
    free(kbuf);
    free(vbuf);
}

static void test_prefix(void) {
    fkv_init();
    insert_sample("123", "45");
    insert_sample("124", "67");
    insert_sample("129", "89");

    uint8_t prefix[] = {1, 2};
    fkv_iter_t it = {0};
    assert(fkv_get_prefix(prefix, sizeof(prefix), &it, 3) == 0);
    assert(it.count >= 2);
    for (size_t i = 0; i < it.count; ++i) {
        assert(it.entries[i].key_len >= 2);
    }
    fkv_iter_free(&it);
    fkv_shutdown();
}

int main(void) {
    test_prefix();
    printf("fkv tests passed\n");
    return 0;
}
