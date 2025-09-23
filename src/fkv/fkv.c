#include "fkv/fkv.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct fkv_node {
    struct fkv_node *child[10];
    uint8_t *value;
    size_t value_len;
    uint8_t *key;
    size_t key_len;
} fkv_node_t;

static fkv_node_t *root = NULL;
static pthread_mutex_t fkv_lock = PTHREAD_MUTEX_INITIALIZER;

static fkv_node_t *node_create(void) {
    fkv_node_t *n = calloc(1, sizeof(fkv_node_t));
    return n;
}

int fkv_init(void) {
    pthread_mutex_lock(&fkv_lock);
    if (!root) {
        root = node_create();
        if (!root) {
            pthread_mutex_unlock(&fkv_lock);
            return -1;
        }
    }
    pthread_mutex_unlock(&fkv_lock);
    return 0;
}

static void node_free(fkv_node_t *node) {
    if (!node) {
        return;
    }
    for (int i = 0; i < 10; ++i) {
        node_free(node->child[i]);
    }
    free(node->value);
    free(node->key);
    free(node);
}

void fkv_shutdown(void) {
    pthread_mutex_lock(&fkv_lock);
    node_free(root);
    root = NULL;
    pthread_mutex_unlock(&fkv_lock);
}

int fkv_put(const uint8_t *key, size_t kn, const uint8_t *val, size_t vn) {
    if (!key || !val) {
        return -1;
    }
    if (!root) {
        if (fkv_init() != 0) {
            return -1;
        }
    }
    pthread_mutex_lock(&fkv_lock);
    fkv_node_t *node = root;
    for (size_t i = 0; i < kn; ++i) {
        uint8_t idx = key[i];
        if (idx > 9) {
            pthread_mutex_unlock(&fkv_lock);
            return -1;
        }
        if (!node->child[idx]) {
            node->child[idx] = node_create();
            if (!node->child[idx]) {
                pthread_mutex_unlock(&fkv_lock);
                return -1;
            }
        }
        node = node->child[idx];
    }
    free(node->value);
    free(node->key);
    node->value = malloc(vn);
    node->key = malloc(kn);
    if (!node->value || !node->key) {
        free(node->value);
        node->value = NULL;
        free(node->key);
        node->key = NULL;
        pthread_mutex_unlock(&fkv_lock);
        return -1;
    }
    memcpy(node->value, val, vn);
    node->value_len = vn;
    memcpy(node->key, key, kn);
    node->key_len = kn;
    pthread_mutex_unlock(&fkv_lock);
    return 0;
}

static void collect_entries(const fkv_node_t *node, fkv_entry_t *entries, size_t *count, size_t limit) {
    if (!node || *count >= limit) {
        return;
    }
    if (node->value && *count < limit) {
        entries[*count].key = node->key;
        entries[*count].key_len = node->key_len;
        entries[*count].value = node->value;
        entries[*count].value_len = node->value_len;
        (*count)++;
    }
    for (int i = 0; i < 10 && *count < limit; ++i) {
        collect_entries(node->child[i], entries, count, limit);
    }
}

int fkv_get_prefix(const uint8_t *key, size_t kn, fkv_iter_t *it, size_t k) {
    if (!it || !root) {
        return -1;
    }
    pthread_mutex_lock(&fkv_lock);
    const fkv_node_t *node = root;
    for (size_t i = 0; i < kn; ++i) {
        uint8_t idx = key[i];
        if (idx > 9) {
            pthread_mutex_unlock(&fkv_lock);
            return -1;
        }
        node = node->child[idx];
        if (!node) {
            pthread_mutex_unlock(&fkv_lock);
            it->entries = NULL;
            it->count = 0;
            return 0;
        }
    }
    size_t limit = k ? k : 1;
    fkv_entry_t *entries = calloc(limit, sizeof(fkv_entry_t));
    size_t count = 0;
    collect_entries(node, entries, &count, limit);
    pthread_mutex_unlock(&fkv_lock);

    it->entries = entries;
    it->count = count;
    return 0;
}

void fkv_iter_free(fkv_iter_t *it) {
    if (!it || !it->entries) {
        return;
    }
    free(it->entries);
    it->entries = NULL;
    it->count = 0;
}
