/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "fkv/fkv.h"
#include "fkv/persistence.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

typedef struct fkv_node {
    struct fkv_node *children[10];
    uint8_t *key;
    size_t key_len;
    uint8_t *value;
    size_t value_len;
    fkv_entry_type_t type;
} fkv_node_t;

static pthread_mutex_t fkv_lock = PTHREAD_MUTEX_INITIALIZER;
static fkv_node_t *fkv_root = NULL;
static bool g_replaying = false;

static int fkv_put_locked(const uint8_t *key,
                          size_t kn,
                          const uint8_t *val,
                          size_t vn,
                          fkv_entry_type_t type);
static void count_entries(const fkv_node_t *node, size_t *count);

static fkv_node_t *node_create(void) {
    return calloc(1, sizeof(fkv_node_t));
}

static void node_free(fkv_node_t *node) {
    if (!node) {
        return;
    }
    for (size_t i = 0; i < 10; ++i) {
        node_free(node->children[i]);
    }
    free(node->key);
    free(node->value);
    free(node);
}

static int ensure_root_locked(void) {
    if (!fkv_root) {
        fkv_root = node_create();
    }
    return fkv_root ? 0 : -1;
}

static int persistence_apply_cb(const uint8_t *key,
                                size_t key_len,
                                const uint8_t *value,
                                size_t value_len,
                                fkv_entry_type_t type,
                                void *userdata) {
    (void)userdata;
    return fkv_put_locked(key, key_len, value, value_len, type);
}

static int fkv_put_locked(const uint8_t *key,
                          size_t kn,
                          const uint8_t *val,
                          size_t vn,
                          fkv_entry_type_t type) {
    if (ensure_root_locked() != 0) {
        return -1;
    }

    fkv_node_t *node = fkv_root;
    for (size_t i = 0; i < kn; ++i) {
        uint8_t idx = key[i];
        if (idx > 9) {
            return -1;
        }
        if (!node->children[idx]) {
            node->children[idx] = node_create();
            if (!node->children[idx]) {
                return -1;
            }
        }
        node = node->children[idx];
    }

    uint8_t *new_key = malloc(kn);
    uint8_t *new_value = malloc(vn);
    if (!new_key || !new_value) {
        free(new_key);
        free(new_value);
        return -1;
    }
    memcpy(new_key, key, kn);
    memcpy(new_value, val, vn);

    if (!g_replaying && fkv_persistence_enabled()) {
        if (fkv_persistence_record_put(key, kn, val, vn, type) != 0) {
            free(new_key);
            free(new_value);
            return -1;
        }
    }

    uint8_t *old_key = node->key;
    uint8_t *old_value = node->value;

    node->key = new_key;
    node->key_len = kn;
    node->value = new_value;
    node->value_len = vn;
    node->type = type;

    free(old_key);
    free(old_value);

    return 0;
}

int fkv_init(void) {
    pthread_mutex_lock(&fkv_lock);
    int rc = ensure_root_locked();
    if (rc == 0 && fkv_persistence_enabled()) {
        bool previous = g_replaying;
        g_replaying = true;
        rc = fkv_persistence_start(persistence_apply_cb, NULL);
        g_replaying = previous;
    }
    pthread_mutex_unlock(&fkv_lock);
    return rc;
}

void fkv_shutdown(void) {
    pthread_mutex_lock(&fkv_lock);
    fkv_persistence_shutdown();
    node_free(fkv_root);
    fkv_root = NULL;
    pthread_mutex_unlock(&fkv_lock);
}

int fkv_put(const uint8_t *key,
            size_t kn,
            const uint8_t *val,
            size_t vn,
            fkv_entry_type_t type) {
    if (!key || !val || kn == 0 || vn == 0) {
        return -1;
    }

    pthread_mutex_lock(&fkv_lock);
    int rc = fkv_put_locked(key, kn, val, vn, type);
    pthread_mutex_unlock(&fkv_lock);
    return rc;
}

static void collect_entries(const fkv_node_t *node,
                            fkv_entry_t *entries,
                            size_t *count,
                            size_t limit) {
    if (!node || *count >= limit) {
        return;
    }
    if (node->value && *count < limit) {
        entries[*count].key = node->key;
        entries[*count].key_len = node->key_len;
        entries[*count].value = node->value;
        entries[*count].value_len = node->value_len;
        entries[*count].type = node->type;
        (*count)++;
    }
    for (size_t i = 0; i < 10 && *count < limit; ++i) {
        collect_entries(node->children[i], entries, count, limit);
    }
}

int fkv_get_prefix(const uint8_t *key, size_t kn, fkv_iter_t *it, size_t k) {
    if (!it) {
        return -1;
    }
    it->entries = NULL;
    it->count = 0;

    pthread_mutex_lock(&fkv_lock);
    if (!fkv_root) {
        pthread_mutex_unlock(&fkv_lock);
        return 0;
    }

    const fkv_node_t *node = fkv_root;
    for (size_t i = 0; i < kn; ++i) {
        if (!key) {
            pthread_mutex_unlock(&fkv_lock);
            return -1;
        }
        uint8_t idx = key[i];
        if (idx > 9) {
            pthread_mutex_unlock(&fkv_lock);
            return -1;
        }
        node = node->children[idx];
        if (!node) {
            pthread_mutex_unlock(&fkv_lock);
            return 0;
        }
    }

    size_t limit = k;
    if (limit == 0) {
        size_t total = 0;
        count_entries(node, &total);
        limit = total;
    }
    if (limit == 0) {
        pthread_mutex_unlock(&fkv_lock);
        return 0;
    }
    fkv_entry_t *entries = calloc(limit, sizeof(fkv_entry_t));
    if (!entries) {
        pthread_mutex_unlock(&fkv_lock);
        return -1;
    }

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

static void count_entries(const fkv_node_t *node, size_t *count) {
    if (!node) {
        return;
    }
    if (node->value) {
        (*count)++;
    }
    for (size_t i = 0; i < 10; ++i) {
        count_entries(node->children[i], count);
    }
}

static int gz_write_exact(gzFile fp, const void *data, size_t len) {
    if (len == 0) {
        return 0;
    }
    int written = gzwrite(fp, data, (unsigned int)len);
    return written == (int)len ? 0 : -1;
}

static int gz_read_exact(gzFile fp, void *buffer, size_t len) {
    if (len == 0) {
        return 0;
    }
    int read = gzread(fp, buffer, (unsigned int)len);
    return read == (int)len ? 0 : -1;
}

static int serialize_node(gzFile fp, const fkv_node_t *node) {
    if (!node) {
        return 0;
    }
    if (node->value) {
        uint64_t key_len = node->key_len;
        uint64_t value_len = node->value_len;
        uint8_t type = (uint8_t)node->type;
        if (gz_write_exact(fp, &key_len, sizeof(key_len)) != 0) {
            return -1;
        }
        if (key_len && gz_write_exact(fp, node->key, key_len) != 0) {
            return -1;
        }
        if (gz_write_exact(fp, &value_len, sizeof(value_len)) != 0) {
            return -1;
        }
        if (value_len && gz_write_exact(fp, node->value, value_len) != 0) {
            return -1;
        }
        if (gz_write_exact(fp, &type, sizeof(type)) != 0) {
            return -1;
        }
    }
    for (size_t i = 0; i < 10; ++i) {
        if (serialize_node(fp, node->children[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int fkv_save(const char *path) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    gzFile fp = gzopen(path, "wb");
    if (!fp) {
        return -1;
    }

    pthread_mutex_lock(&fkv_lock);
    size_t count = 0;
    count_entries(fkv_root, &count);
    uint64_t stored = (uint64_t)count;
    int rc = 0;
    if (gz_write_exact(fp, &stored, sizeof(stored)) != 0) {
        rc = -1;
    } else if (fkv_root && serialize_node(fp, fkv_root) != 0) {
        rc = -1;
    }
    pthread_mutex_unlock(&fkv_lock);

    if (gzclose(fp) != Z_OK) {
        rc = -1;
    }
    return rc;
}

int fkv_load(const char *path) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    gzFile fp = gzopen(path, "rb");
    if (!fp) {
        return -1;
    }

    uint64_t count = 0;
    if (gz_read_exact(fp, &count, sizeof(count)) != 0) {
        gzclose(fp);
        return -1;
    }

    pthread_mutex_lock(&fkv_lock);
    node_free(fkv_root);
    fkv_root = node_create();
    if (!fkv_root && count > 0) {
        pthread_mutex_unlock(&fkv_lock);
        gzclose(fp);
        return -1;
    }
    pthread_mutex_unlock(&fkv_lock);

    int rc = 0;
    bool previous = g_replaying;
    g_replaying = true;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t key_len = 0;
        uint64_t value_len = 0;
        uint8_t type = 0;

        if (gz_read_exact(fp, &key_len, sizeof(key_len)) != 0) {
            rc = -1;
            break;
        }
        uint8_t *key_buf = NULL;
        if (key_len > 0) {
            key_buf = malloc((size_t)key_len);
            if (!key_buf || gz_read_exact(fp, key_buf, (size_t)key_len) != 0) {
                free(key_buf);
                rc = -1;
                break;
            }
        }

        if (gz_read_exact(fp, &value_len, sizeof(value_len)) != 0) {
            free(key_buf);
            rc = -1;
            break;
        }
        uint8_t *value_buf = NULL;
        if (value_len > 0) {
            value_buf = malloc((size_t)value_len);
            if (!value_buf || gz_read_exact(fp, value_buf, (size_t)value_len) != 0) {
                free(key_buf);
                free(value_buf);
                rc = -1;
                break;
            }
        }

        if (gz_read_exact(fp, &type, sizeof(type)) != 0) {
            free(key_buf);
            free(value_buf);
            rc = -1;
            break;
        }

        if (fkv_put(key_buf, (size_t)key_len, value_buf, (size_t)value_len, (fkv_entry_type_t)type) != 0) {
            free(key_buf);
            free(value_buf);
            rc = -1;
            break;
        }

        free(key_buf);
        free(value_buf);
    }
    g_replaying = previous;
    gzclose(fp);
    return rc;
}
