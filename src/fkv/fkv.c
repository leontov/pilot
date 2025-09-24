/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "fkv/fkv.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fkv_entry_payload {
    uint8_t *key;
    size_t key_len;
    uint8_t *value;
    size_t value_len;
    fkv_entry_type_t type;
    double score;
} fkv_entry_payload_t;

typedef struct fkv_node {
    struct fkv_node *children[10];
    fkv_entry_payload_t *payload;
    fkv_entry_payload_t **top_entries;
    size_t top_count;
    size_t top_capacity;
} fkv_node_t;

static pthread_mutex_t fkv_lock = PTHREAD_MUTEX_INITIALIZER;
static fkv_node_t *fkv_root = NULL;
static size_t fkv_top_k_limit_value = 8;
static const size_t FKV_DEFAULT_TOP_K = 8;
static const size_t FKV_MAX_TOP_K = 1024;

static fkv_node_t *node_create(void) {
    return calloc(1, sizeof(fkv_node_t));
}

static void payload_destroy(fkv_entry_payload_t *payload) {
    if (!payload) {
        return;
    }
    free(payload->key);
    free(payload->value);
    free(payload);
}

static void node_free(fkv_node_t *node) {
    if (!node) {
        return;
    }
    for (size_t i = 0; i < 10; ++i) {
        node_free(node->children[i]);
    }
    payload_destroy(node->payload);
    free(node->top_entries);
    free(node);
}

static int ensure_root_locked(void) {
    if (!fkv_root) {
        fkv_root = node_create();
    }
    return fkv_root ? 0 : -1;
}

static int node_reserve_top(fkv_node_t *node, size_t capacity) {
    if (!node || capacity == 0) {
        return 0;
    }
    if (node->top_capacity >= capacity) {
        return 0;
    }
    fkv_entry_payload_t **entries =
        realloc(node->top_entries, capacity * sizeof(*entries));
    if (!entries) {
        return -1;
    }
    node->top_entries = entries;
    node->top_capacity = capacity;
    return 0;
}

static int compare_payloads(const void *lhs, const void *rhs) {
    const fkv_entry_payload_t *a = *(const fkv_entry_payload_t *const *)lhs;
    const fkv_entry_payload_t *b = *(const fkv_entry_payload_t *const *)rhs;
    if (a->score < b->score) {
        return 1;
    }
    if (a->score > b->score) {
        return -1;
    }
    size_t min_len = a->key_len < b->key_len ? a->key_len : b->key_len;
    int cmp = memcmp(a->key, b->key, min_len);
    if (cmp != 0) {
        return cmp;
    }
    if (a->key_len < b->key_len) {
        return -1;
    }
    if (a->key_len > b->key_len) {
        return 1;
    }
    return 0;
}

static int node_update_top(fkv_node_t *node) {
    if (!node) {
        return 0;
    }
    size_t limit = fkv_top_k_limit_value;
    if (limit == 0) {
        node->top_count = 0;
        return 0;
    }

    size_t candidate_capacity = 1 + 10 * limit;
    fkv_entry_payload_t **candidates =
        malloc(candidate_capacity * sizeof(*candidates));
    if (!candidates) {
        return -1;
    }
    size_t count = 0;
    if (node->payload) {
        candidates[count++] = node->payload;
    }
    for (size_t i = 0; i < 10; ++i) {
        fkv_node_t *child = node->children[i];
        if (!child) {
            continue;
        }
        for (size_t j = 0; j < child->top_count && count < candidate_capacity; ++j) {
            candidates[count++] = child->top_entries[j];
        }
    }

    if (count == 0) {
        node->top_count = 0;
        free(candidates);
        return 0;
    }

    qsort(candidates, count, sizeof(*candidates), compare_payloads);

    if (node_reserve_top(node, limit) != 0) {
        free(candidates);
        return -1;
    }

    size_t new_count = count < limit ? count : limit;
    for (size_t i = 0; i < new_count; ++i) {
        node->top_entries[i] = candidates[i];
    }
    node->top_count = new_count;

    free(candidates);
    return 0;
}

static int node_set_payload(fkv_node_t *node,
                            const uint8_t *key,
                            size_t key_len,
                            const uint8_t *value,
                            size_t value_len,
                            fkv_entry_type_t type,
                            double score) {
    if (!node) {
        return -1;
    }

    fkv_entry_payload_t *payload = node->payload;
    int created = 0;
    if (!payload) {
        payload = calloc(1, sizeof(*payload));
        if (!payload) {
            return -1;
        }
        node->payload = payload;
        created = 1;
    }

    uint8_t *key_copy = malloc(key_len);
    uint8_t *value_copy = malloc(value_len);
    if (!key_copy || !value_copy) {
        free(key_copy);
        free(value_copy);
        if (created) {
            payload_destroy(payload);
            node->payload = NULL;
        }
        return -1;
    }

    memcpy(key_copy, key, key_len);
    memcpy(value_copy, value, value_len);

    free(payload->key);
    free(payload->value);

    payload->key = key_copy;
    payload->key_len = key_len;
    payload->value = value_copy;
    payload->value_len = value_len;
    payload->type = type;
    payload->score = score;

    return 0;
}

static int fkv_put_locked(const uint8_t *key,
                          size_t kn,
                          const uint8_t *val,
                          size_t vn,
                          fkv_entry_type_t type,
                          double score) {
    if (ensure_root_locked() != 0) {
        return -1;
    }

    fkv_node_t **path = calloc(kn + 1, sizeof(*path));
    if (!path) {
        return -1;
    }

    fkv_node_t *node = fkv_root;
    path[0] = node;
    for (size_t i = 0; i < kn; ++i) {
        uint8_t idx = key[i];
        if (idx > 9) {
            free(path);
            return -1;
        }
        if (!node->children[idx]) {
            node->children[idx] = node_create();
            if (!node->children[idx]) {
                free(path);
                return -1;
            }
        }
        node = node->children[idx];
        path[i + 1] = node;
    }

    int rc = node_set_payload(node, key, kn, val, vn, type, score);
    if (rc == 0) {
        for (size_t i = kn + 1; i-- > 0;) {
            if (node_update_top(path[i]) != 0) {
                rc = -1;
                break;
            }
        }
    }

    free(path);
    return rc;
}

int fkv_init(size_t top_k_limit) {
    if (top_k_limit == 0) {
        top_k_limit = FKV_DEFAULT_TOP_K;
    }
    if (top_k_limit > FKV_MAX_TOP_K) {
        top_k_limit = FKV_MAX_TOP_K;
    }

    pthread_mutex_lock(&fkv_lock);
    node_free(fkv_root);
    fkv_root = NULL;
    fkv_top_k_limit_value = top_k_limit;
    int rc = ensure_root_locked();
    pthread_mutex_unlock(&fkv_lock);
    return rc;
}

void fkv_shutdown(void) {
    pthread_mutex_lock(&fkv_lock);
    node_free(fkv_root);
    fkv_root = NULL;
    pthread_mutex_unlock(&fkv_lock);
}

size_t fkv_get_top_k_limit(void) {
    pthread_mutex_lock(&fkv_lock);
    size_t limit = fkv_top_k_limit_value;
    pthread_mutex_unlock(&fkv_lock);
    return limit;
}

int fkv_put(const uint8_t *key,
            size_t kn,
            const uint8_t *val,
            size_t vn,
            fkv_entry_type_t type,
            double score) {
    if (!key || !val || kn == 0 || vn == 0 || !isfinite(score)) {
        return -1;
    }

    pthread_mutex_lock(&fkv_lock);
    int rc = fkv_put_locked(key, kn, val, vn, type, score);
    pthread_mutex_unlock(&fkv_lock);
    return rc;
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
    if (key && kn > 0) {
        for (size_t i = 0; i < kn; ++i) {
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
    }

    size_t available = node->top_count;
    if (available == 0) {
        pthread_mutex_unlock(&fkv_lock);
        return 0;
    }

    size_t limit = available;
    if (k > 0 && k < limit) {
        limit = k;
    }

    fkv_entry_t *entries = calloc(limit, sizeof(*entries));
    if (!entries) {
        pthread_mutex_unlock(&fkv_lock);
        return -1;
    }

    for (size_t i = 0; i < limit; ++i) {
        const fkv_entry_payload_t *payload = node->top_entries[i];
        entries[i].key = payload->key;
        entries[i].key_len = payload->key_len;
        entries[i].value = payload->value;
        entries[i].value_len = payload->value_len;
        entries[i].type = payload->type;
        entries[i].score = payload->score;
    }

    pthread_mutex_unlock(&fkv_lock);

    it->entries = entries;
    it->count = limit;
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
    if (node->payload) {
        (*count)++;
    }
    for (size_t i = 0; i < 10; ++i) {
        count_entries(node->children[i], count);
    }
}

static int serialize_node(FILE *fp, const fkv_node_t *node) {
    if (!node) {
        return 0;
    }
    if (node->payload) {
        uint64_t key_len = node->payload->key_len;
        uint64_t value_len = node->payload->value_len;
        uint8_t type = (uint8_t)node->payload->type;
        double score = node->payload->score;
        if (fwrite(&key_len, sizeof(key_len), 1, fp) != 1) {
            return -1;
        }
        if (key_len && fwrite(node->payload->key, 1, key_len, fp) != key_len) {
            return -1;
        }
        if (fwrite(&value_len, sizeof(value_len), 1, fp) != 1) {
            return -1;
        }
        if (value_len && fwrite(node->payload->value, 1, value_len, fp) != value_len) {
            return -1;
        }
        if (fwrite(&type, sizeof(type), 1, fp) != 1) {
            return -1;
        }
        if (fwrite(&score, sizeof(score), 1, fp) != 1) {
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

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }

    pthread_mutex_lock(&fkv_lock);
    size_t count = 0;
    count_entries(fkv_root, &count);
    uint64_t stored = (uint64_t)count;
    int rc = 0;
    if (fwrite(&stored, sizeof(stored), 1, fp) != 1) {
        rc = -1;
    } else if (fkv_root && serialize_node(fp, fkv_root) != 0) {
        rc = -1;
    }
    pthread_mutex_unlock(&fkv_lock);

    if (fclose(fp) != 0) {
        rc = -1;
    }
    return rc;
}

static int read_exact(FILE *fp, void *buffer, size_t len) {
    return len == 0 || fread(buffer, 1, len, fp) == len ? 0 : -1;
}

int fkv_load(const char *path) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }

    uint64_t count = 0;
    if (fread(&count, sizeof(count), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    pthread_mutex_lock(&fkv_lock);
    node_free(fkv_root);
    fkv_root = node_create();
    if (!fkv_root && count > 0) {
        pthread_mutex_unlock(&fkv_lock);
        fclose(fp);
        return -1;
    }
    pthread_mutex_unlock(&fkv_lock);

    int rc = 0;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t key_len = 0;
        uint64_t value_len = 0;
        uint8_t type = 0;
        double score = 0.0;

        if (fread(&key_len, sizeof(key_len), 1, fp) != 1) {
            rc = -1;
            break;
        }
        uint8_t *key_buf = NULL;
        if (key_len > 0) {
            key_buf = malloc((size_t)key_len);
            if (!key_buf || read_exact(fp, key_buf, (size_t)key_len) != 0) {
                free(key_buf);
                rc = -1;
                break;
            }
        }

        if (fread(&value_len, sizeof(value_len), 1, fp) != 1) {
            free(key_buf);
            rc = -1;
            break;
        }
        uint8_t *value_buf = NULL;
        if (value_len > 0) {
            value_buf = malloc((size_t)value_len);
            if (!value_buf || read_exact(fp, value_buf, (size_t)value_len) != 0) {
                free(key_buf);
                free(value_buf);
                rc = -1;
                break;
            }
        }

        if (fread(&type, sizeof(type), 1, fp) != 1) {
            free(key_buf);
            free(value_buf);
            rc = -1;
            break;
        }

        if (fread(&score, sizeof(score), 1, fp) != 1) {
            free(key_buf);
            free(value_buf);
            rc = -1;
            break;
        }

        if (fkv_put(key_buf,
                    (size_t)key_len,
                    value_buf,
                    (size_t)value_len,
                    (fkv_entry_type_t)type,
                    score) != 0) {
            rc = -1;
        }

        free(key_buf);
        free(value_buf);
        if (rc != 0) {
            break;
        }
    }

    fclose(fp);
    return rc;
}
