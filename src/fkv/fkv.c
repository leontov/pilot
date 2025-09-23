#include "fkv/fkv.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fkv_node {
    struct fkv_node *child[10];
    uint8_t *value;
    size_t value_len;
    uint8_t *key;
    size_t key_len;
    fkv_entry_type_t type;
} fkv_node_t;

static fkv_node_t *root = NULL;
static pthread_mutex_t fkv_lock = PTHREAD_MUTEX_INITIALIZER;

static fkv_node_t *node_create(void) {
    fkv_node_t *n = calloc(1, sizeof(fkv_node_t));
    return n;
}

static void node_free(fkv_node_t *node);
static int fkv_put_locked(const uint8_t *key, size_t kn, const uint8_t *val, size_t vn,
                          fkv_entry_type_t type);

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

static int fkv_put_locked(const uint8_t *key, size_t kn, const uint8_t *val, size_t vn,
                          fkv_entry_type_t type) {
    if (!root) {
        root = node_create();
        if (!root) {
            return -1;
        }
    }

    fkv_node_t *node = root;
    for (size_t i = 0; i < kn; ++i) {
        uint8_t idx = key[i];
        if (idx > 9) {
            return -1;
        }
        if (!node->child[idx]) {
            node->child[idx] = node_create();
            if (!node->child[idx]) {
                return -1;
            }
        }
        node = node->child[idx];
    }

    uint8_t *new_value = malloc(vn);
    uint8_t *new_key = malloc(kn);
    if (!new_value || !new_key) {
        free(new_value);
        free(new_key);
        return -1;
    }

    memcpy(new_value, val, vn);
    memcpy(new_key, key, kn);

    free(node->value);
    free(node->key);

    node->value = new_value;
    node->value_len = vn;
    node->key = new_key;
    node->key_len = kn;
    node->type = type;

    return 0;
}

int fkv_put(const uint8_t *key, size_t kn, const uint8_t *val, size_t vn, fkv_entry_type_t type) {
    if (!key || !val || kn == 0 || vn == 0) {
        return -1;
    }
    pthread_mutex_lock(&fkv_lock);
    int rc = fkv_put_locked(key, kn, val, vn, type);
    pthread_mutex_unlock(&fkv_lock);
    return rc;
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
        entries[*count].type = node->type;
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

static void count_entries(const fkv_node_t *node, size_t *count) {
    if (!node) {
        return;
    }
    if (node->value) {
        (*count)++;
    }
    for (int i = 0; i < 10; ++i) {
        count_entries(node->child[i], count);
    }
}

static int serialize_node(FILE *fp, const fkv_node_t *node) {
    if (!node) {
        return 0;
    }
    if (node->value) {
        uint64_t key_len = node->key_len;
        uint64_t value_len = node->value_len;
        uint8_t type = (uint8_t)node->type;
        if (fwrite(&key_len, sizeof(key_len), 1, fp) != 1) {
            return -1;
        }
        if (node->key_len && fwrite(node->key, 1, node->key_len, fp) != node->key_len) {
            return -1;
        }
        if (fwrite(&value_len, sizeof(value_len), 1, fp) != 1) {
            return -1;
        }
        if (node->value_len && fwrite(node->value, 1, node->value_len, fp) != node->value_len) {
            return -1;
        }
        if (fwrite(&type, sizeof(type), 1, fp) != 1) {
            return -1;
        }
    }
    for (int i = 0; i < 10; ++i) {
        if (serialize_node(fp, node->child[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

int fkv_save(const char *path) {
    if (!path) {
        return -1;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }

    pthread_mutex_lock(&fkv_lock);
    size_t entry_count = 0;
    if (root) {
        count_entries(root, &entry_count);
    }
    uint64_t count64 = entry_count;
    int rc = 0;
    if (fwrite(&count64, sizeof(count64), 1, fp) != 1) {
        rc = -1;
    } else if (root && serialize_node(fp, root) != 0) {
        rc = -1;
    }
    pthread_mutex_unlock(&fkv_lock);

    if (fclose(fp) != 0) {
        rc = -1;
    }
    if (rc != 0) {
        remove(path);
    }
    return rc;
}

int fkv_load(const char *path) {
    if (!path) {
        return -1;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }

    uint64_t count64 = 0;
    if (fread(&count64, sizeof(count64), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    pthread_mutex_lock(&fkv_lock);
    node_free(root);
    root = node_create();
    if (!root) {
        pthread_mutex_unlock(&fkv_lock);
        fclose(fp);
        return -1;
    }

    int rc = 0;
    for (uint64_t i = 0; i < count64; ++i) {
        uint64_t key_len64 = 0;
        uint64_t value_len64 = 0;
        if (fread(&key_len64, sizeof(key_len64), 1, fp) != 1) {
            rc = -1;
            break;
        }
        if (key_len64 == 0 || key_len64 > SIZE_MAX) {
            rc = -1;
            break;
        }
        size_t key_len = (size_t)key_len64;
        uint8_t *key_buf = NULL;
        if (key_len > 0) {
            key_buf = malloc(key_len);
            if (!key_buf) {
                rc = -1;
                break;
            }
            if (fread(key_buf, 1, key_len, fp) != key_len) {
                free(key_buf);
                rc = -1;
                break;
            }
        }
        if (fread(&value_len64, sizeof(value_len64), 1, fp) != 1) {
            free(key_buf);
            rc = -1;
            break;
        }
        if (value_len64 == 0 || value_len64 > SIZE_MAX) {
            free(key_buf);
            rc = -1;
            break;
        }
        size_t value_len = (size_t)value_len64;
        uint8_t *value_buf = NULL;
        if (value_len > 0) {
            value_buf = malloc(value_len);
            if (!value_buf) {
                free(key_buf);
                rc = -1;
                break;
            }
            if (fread(value_buf, 1, value_len, fp) != value_len) {
                free(key_buf);
                free(value_buf);
                rc = -1;
                break;
            }
        }
        uint8_t type_raw = 0;
        if (fread(&type_raw, sizeof(type_raw), 1, fp) != 1) {
            free(key_buf);
            free(value_buf);
            rc = -1;
            break;
        }
        if (type_raw > (uint8_t)FKV_ENTRY_TYPE_PROGRAM) {
            free(key_buf);
            free(value_buf);
            rc = -1;
            break;
        }

        if (fkv_put_locked(key_buf, key_len, value_buf, value_len, (fkv_entry_type_t)type_raw) != 0) {
            free(key_buf);
            free(value_buf);
            rc = -1;
            break;
        }
        free(key_buf);
        free(value_buf);
    }

    if (rc != 0) {
        node_free(root);
        root = node_create();
        if (!root) {
            rc = -1;
        }
    }

    pthread_mutex_unlock(&fkv_lock);

    if (fclose(fp) != 0) {
        rc = -1;
    }
    return rc;
}
