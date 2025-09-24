/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "fkv/fkv.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fkv_entry_record {
    uint8_t *key;
    size_t key_len;
    uint8_t *value;
    size_t value_len;
    fkv_entry_type_t type;
    uint64_t priority;
} fkv_entry_record_t;

typedef struct fkv_node {
    struct fkv_node *children[10];
    fkv_entry_record_t *self_entry;
    fkv_entry_record_t **top_entries;
    size_t top_count;
    size_t top_capacity;
} fkv_node_t;

static pthread_mutex_t fkv_lock = PTHREAD_MUTEX_INITIALIZER;
static fkv_node_t *fkv_root = NULL;
static size_t fkv_topk_limit = 4;
static uint64_t fkv_sequence = 1;

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
    if (node->self_entry) {
        free(node->self_entry->key);
        free(node->self_entry->value);
        free(node->self_entry);
    }
    free(node->top_entries);
    free(node);
}

static void node_prune_entries(fkv_node_t *node) {
    if (!node) {
        return;
    }
    if (node->top_count > fkv_topk_limit) {
        node->top_count = fkv_topk_limit;
    }
    for (size_t i = 0; i < 10; ++i) {
        node_prune_entries(node->children[i]);
    }
}

static int ensure_root_locked(void) {
    if (!fkv_root) {
        fkv_root = node_create();
    }
    return fkv_root ? 0 : -1;
}

static fkv_entry_record_t *entry_create(const uint8_t *key,
                                       size_t kn,
                                       const uint8_t *val,
                                       size_t vn,
                                       fkv_entry_type_t type,
                                       uint64_t priority) {
    fkv_entry_record_t *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return NULL;
    }
    entry->key = malloc(kn);
    entry->value = malloc(vn);
    if ((!entry->key && kn > 0) || (!entry->value && vn > 0)) {
        free(entry->key);
        free(entry->value);
        free(entry);
        return NULL;
    }
    if (kn > 0) {
        memcpy(entry->key, key, kn);
    }
    if (vn > 0) {
        memcpy(entry->value, val, vn);
    }
    entry->key_len = kn;
    entry->value_len = vn;
    entry->type = type;
    entry->priority = priority;
    return entry;
}

static void node_remove_top_entry(fkv_node_t *node, const fkv_entry_record_t *entry) {
    if (!node || !entry || node->top_count == 0) {
        return;
    }
    for (size_t i = 0; i < node->top_count; ++i) {
        if (node->top_entries[i] == entry) {
            if (i + 1 < node->top_count) {
                memmove(node->top_entries + i,
                        node->top_entries + i + 1,
                        (node->top_count - i - 1) * sizeof(node->top_entries[0]));
            }
            node->top_count--;
            break;
        }
    }
}

static int node_ensure_capacity(fkv_node_t *node, size_t needed) {
    if (!node) {
        return -1;
    }
    if (node->top_capacity >= needed) {
        return 0;
    }
    size_t new_capacity = node->top_capacity ? node->top_capacity : fkv_topk_limit;
    if (new_capacity == 0) {
        new_capacity = 1;
    }
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    fkv_entry_record_t **tmp =
        realloc(node->top_entries, new_capacity * sizeof(*tmp));
    if (!tmp) {
        return -1;
    }
    node->top_entries = tmp;
    node->top_capacity = new_capacity;
    return 0;
}

static int node_insert_top_entry(fkv_node_t *node, fkv_entry_record_t *entry) {
    if (!node || !entry) {
        return -1;
    }
    node_remove_top_entry(node, entry);
    if (fkv_topk_limit == 0) {
        return 0;
    }
    size_t needed = node->top_count + 1;
    if (needed < fkv_topk_limit) {
        needed = fkv_topk_limit;
    }
    if (node_ensure_capacity(node, needed) != 0) {
        return -1;
    }
    size_t insert_pos = node->top_count;
    while (insert_pos > 0 && node->top_entries[insert_pos - 1]->priority < entry->priority) {
        insert_pos--;
    }
    if (insert_pos < node->top_count) {
        memmove(node->top_entries + insert_pos + 1,
                node->top_entries + insert_pos,
                (node->top_count - insert_pos) * sizeof(node->top_entries[0]));
    }
    node->top_entries[insert_pos] = entry;
    node->top_count++;
    if (node->top_count > fkv_topk_limit) {
        node->top_count = fkv_topk_limit;
    }
    return 0;
}

static void fkv_delta_entry_cleanup(fkv_delta_entry_t *entry) {
    if (!entry) {
        return;
    }
    free(entry->key);
    free(entry->value);
    entry->key = NULL;
    entry->value = NULL;
    entry->key_len = 0;
    entry->value_len = 0;
}

static int fkv_delta_reserve(fkv_delta_t *delta, size_t needed) {
    if (!delta) {
        return -1;
    }
    if (delta->capacity >= needed) {
        return 0;
    }
    size_t new_capacity = delta->capacity ? delta->capacity : 4;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    fkv_delta_entry_t *entries =
        realloc(delta->entries, new_capacity * sizeof(fkv_delta_entry_t));
    if (!entries) {
        return -1;
    }
    for (size_t i = delta->capacity; i < new_capacity; ++i) {
        memset(&entries[i], 0, sizeof(entries[i]));
    }
    delta->entries = entries;
    delta->capacity = new_capacity;
    return 0;
}

static int fkv_delta_append_entry(fkv_delta_t *delta, const fkv_entry_record_t *rec) {
    if (!delta || !rec) {
        return -1;
    }
    size_t next_index = delta->count + 1;
    if (fkv_delta_reserve(delta, next_index) != 0) {
        return -1;
    }
    fkv_delta_entry_t *entry = &delta->entries[delta->count];
    memset(entry, 0, sizeof(*entry));

    if (rec->key_len > 0) {
        entry->key = malloc(rec->key_len);
        if (!entry->key) {
            return -1;
        }
        memcpy(entry->key, rec->key, rec->key_len);
        entry->key_len = rec->key_len;
    }
    if (rec->value_len > 0) {
        entry->value = malloc(rec->value_len);
        if (!entry->value) {
            free(entry->key);
            entry->key = NULL;
            entry->key_len = 0;
            return -1;
        }
        memcpy(entry->value, rec->value, rec->value_len);
        entry->value_len = rec->value_len;
    }
    entry->type = rec->type;
    entry->priority = rec->priority;

    if (delta->count == 0 || rec->priority < delta->min_sequence) {
        delta->min_sequence = rec->priority;
    }
    if (rec->priority > delta->max_sequence) {
        delta->max_sequence = rec->priority;
    }
    delta->total_bytes += entry->key_len + entry->value_len;
    delta->count++;
    return 0;
}

static int fkv_collect_delta_entries(const fkv_node_t *node,
                                     uint64_t since_sequence,
                                     fkv_delta_t *delta) {
    if (!node) {
        return 0;
    }
    if (node->self_entry && node->self_entry->priority > since_sequence) {
        if (fkv_delta_append_entry(delta, node->self_entry) != 0) {
            return -1;
        }
    }
    for (size_t i = 0; i < 10; ++i) {
        if (node->children[i]) {
            if (fkv_collect_delta_entries(node->children[i], since_sequence, delta) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int fkv_put_locked_internal(const uint8_t *key,
                                   size_t kn,
                                   const uint8_t *val,
                                   size_t vn,
                                   fkv_entry_type_t type,
                                   uint64_t priority) {
    if (ensure_root_locked() != 0) {
        return -1;
    }

    size_t depth_capacity = kn + 1;
    if (depth_capacity == 0) {
        depth_capacity = 1;
    }
    fkv_node_t **path = calloc(depth_capacity, sizeof(*path));
    if (!path) {
        return -1;
    }

    int rc = 0;
    fkv_node_t *node = fkv_root;
    size_t depth = 0;
    path[depth++] = node;
    for (size_t i = 0; i < kn; ++i) {
        uint8_t idx = key[i];
        if (idx > 9) {
            rc = -1;
            goto cleanup;
        }
        if (!node->children[idx]) {
            node->children[idx] = node_create();
            if (!node->children[idx]) {
                rc = -1;
                goto cleanup;
            }
        }
        node = node->children[idx];
        if (depth < depth_capacity) {
            path[depth++] = node;
        }
    }

    uint64_t effective_priority = priority ? priority : fkv_sequence++;

    if (node->self_entry) {
        if (node->self_entry->value_len != vn) {
            uint8_t *tmp = realloc(node->self_entry->value, vn);
            if (!tmp && vn > 0) {
                rc = -1;
                goto cleanup;
            }
            node->self_entry->value = tmp;
        }
        if (node->self_entry->key_len != kn) {
            uint8_t *tmp = realloc(node->self_entry->key, kn);
            if (!tmp && kn > 0) {
                rc = -1;
                goto cleanup;
            }
            node->self_entry->key = tmp;
            node->self_entry->key_len = kn;
        }
        if (vn > 0) {
            memcpy(node->self_entry->value, val, vn);
        }
        if (kn > 0) {
            memcpy(node->self_entry->key, key, kn);
        }
        node->self_entry->value_len = vn;
        node->self_entry->type = type;
        node->self_entry->priority = effective_priority;
    } else {
        node->self_entry = entry_create(key, kn, val, vn, type, effective_priority);
        if (!node->self_entry) {
            rc = -1;
            goto cleanup;
        }
    }

    for (size_t i = 0; i < depth; ++i) {
        if (node_insert_top_entry(path[i], node->self_entry) != 0) {
            rc = -1;
            goto cleanup;
        }
    }

    if (effective_priority >= fkv_sequence) {
        fkv_sequence = effective_priority + 1;
    }

cleanup:
    free(path);
    return rc;
}

int fkv_init(void) {
    pthread_mutex_lock(&fkv_lock);
    int rc = ensure_root_locked();
    if (rc == 0) {
        fkv_sequence = 1;
    }
    pthread_mutex_unlock(&fkv_lock);
    return rc;
}

void fkv_shutdown(void) {
    pthread_mutex_lock(&fkv_lock);
    node_free(fkv_root);
    fkv_root = NULL;
    pthread_mutex_unlock(&fkv_lock);
}

int fkv_put(const uint8_t *key,
            size_t kn,
            const uint8_t *val,
            size_t vn,
            fkv_entry_type_t type) {
    return fkv_put_scored(key, kn, val, vn, type, 0);
}

int fkv_put_scored(const uint8_t *key,
                   size_t kn,
                   const uint8_t *val,
                   size_t vn,
                   fkv_entry_type_t type,
                   uint64_t priority) {
    if (!key || !val || kn == 0 || vn == 0) {
        return -1;
    }

    pthread_mutex_lock(&fkv_lock);
    int rc = fkv_put_locked_internal(key, kn, val, vn, type, priority);
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

    size_t limit = k ? k : fkv_topk_limit;
    if (limit == 0) {
        limit = fkv_topk_limit ? fkv_topk_limit : 1;
    }

    fkv_entry_record_t **selected = NULL;
    size_t capacity = limit;
    if (capacity == 0) {
        capacity = 1;
    }
    if (capacity <= 64) {
        static fkv_entry_record_t *stack_entries[64];
        selected = stack_entries;
        capacity = 64;
    } else {
        selected = calloc(capacity, sizeof(*selected));
        if (!selected) {
            pthread_mutex_unlock(&fkv_lock);
            return -1;
        }
    }

    size_t selected_count = 0;
    if (node->self_entry && limit > 0) {
        selected[selected_count++] = node->self_entry;
    }
    for (size_t i = 0; i < node->top_count && selected_count < limit; ++i) {
        int seen = 0;
        for (size_t j = 0; j < selected_count; ++j) {
            if (selected[j] == node->top_entries[i]) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            selected[selected_count++] = node->top_entries[i];
        }
    }

    if (selected_count == 0) {
        pthread_mutex_unlock(&fkv_lock);
        if (capacity > 64) {
            free(selected);
        }
        return 0;
    }

    fkv_entry_t *entries = calloc(selected_count, sizeof(fkv_entry_t));
    if (!entries) {
        pthread_mutex_unlock(&fkv_lock);
        if (capacity > 64) {
            free(selected);
        }
        return -1;
    }

    for (size_t i = 0; i < selected_count; ++i) {
        fkv_entry_record_t *rec = selected[i];
        if (rec->key_len > 0) {
            uint8_t *key_copy = malloc(rec->key_len);
            if (!key_copy) {
                pthread_mutex_unlock(&fkv_lock);
                for (size_t j = 0; j < i; ++j) {
                    free((void *)entries[j].key);
                    free((void *)entries[j].value);
                }
                free(entries);
                if (capacity > 64) {
                    free(selected);
                }
                return -1;
            }
            memcpy(key_copy, rec->key, rec->key_len);
            entries[i].key = key_copy;
        }
        entries[i].key_len = rec->key_len;
        if (rec->value_len > 0) {
            uint8_t *val_copy = malloc(rec->value_len);
            if (!val_copy) {
                pthread_mutex_unlock(&fkv_lock);
                for (size_t j = 0; j <= i; ++j) {
                    free((void *)entries[j].key);
                    free((void *)entries[j].value);
                }
                free(entries);
                if (capacity > 64) {
                    free(selected);
                }
                return -1;
            }
            memcpy(val_copy, rec->value, rec->value_len);
            entries[i].value = val_copy;
        }
        entries[i].value_len = rec->value_len;
        entries[i].type = rec->type;
        entries[i].priority = rec->priority;
    }
    pthread_mutex_unlock(&fkv_lock);

    if (capacity > 64) {
        free(selected);
    }

    it->entries = entries;
    it->count = selected_count;
    return 0;
}

void fkv_iter_free(fkv_iter_t *it) {
    if (!it || !it->entries) {
        return;
    }
    for (size_t i = 0; i < it->count; ++i) {
        free((void *)it->entries[i].key);
        free((void *)it->entries[i].value);
    }
    free(it->entries);
    it->entries = NULL;
    it->count = 0;
}

static void count_entries(const fkv_node_t *node, size_t *count) {
    if (!node) {
        return;
    }
    if (node->self_entry) {
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
    if (node->self_entry) {
        const fkv_entry_record_t *entry = node->self_entry;
        uint64_t key_len = entry->key_len;
        uint64_t value_len = entry->value_len;
        uint8_t type = (uint8_t)entry->type;
        uint64_t priority = entry->priority;
        if (fwrite(&key_len, sizeof(key_len), 1, fp) != 1) {
            return -1;
        }
        if (key_len && fwrite(entry->key, 1, key_len, fp) != key_len) {
            return -1;
        }
        if (fwrite(&value_len, sizeof(value_len), 1, fp) != 1) {
            return -1;
        }
        if (value_len && fwrite(entry->value, 1, value_len, fp) != value_len) {
            return -1;
        }
        if (fwrite(&type, sizeof(type), 1, fp) != 1) {
            return -1;
        }
        if (fwrite(&priority, sizeof(priority), 1, fp) != 1) {
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
        uint64_t priority = 0;

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
        if (fread(&priority, sizeof(priority), 1, fp) != 1) {
            free(key_buf);
            free(value_buf);
            rc = -1;
            break;
        }

        pthread_mutex_lock(&fkv_lock);
        rc = fkv_put_locked_internal(key_buf,
                                     (size_t)key_len,
                                     value_buf,
                                     (size_t)value_len,
                                     (fkv_entry_type_t)type,
                                     priority);
        pthread_mutex_unlock(&fkv_lock);
        if (rc != 0) {
            free(key_buf);
            free(value_buf);
            rc = -1;
            break;
        }

        free(key_buf);
        free(value_buf);
    }

    fclose(fp);
    return rc;
}

void fkv_set_topk_limit(size_t limit) {
    if (limit == 0) {
        limit = 1;
    }
    pthread_mutex_lock(&fkv_lock);
    fkv_topk_limit = limit;
    if (fkv_root) {
        node_prune_entries(fkv_root);
    }
    pthread_mutex_unlock(&fkv_lock);
}

size_t fkv_get_topk_limit(void) {
    pthread_mutex_lock(&fkv_lock);
    size_t limit = fkv_topk_limit;
    pthread_mutex_unlock(&fkv_lock);
    return limit;
}

uint64_t fkv_current_sequence(void) {
    pthread_mutex_lock(&fkv_lock);
    uint64_t seq = fkv_sequence;
    pthread_mutex_unlock(&fkv_lock);
    if (seq == 0) {
        return 0;
    }
    return seq - 1;
}

uint16_t fkv_delta_compute_checksum(const fkv_delta_t *delta) {
    if (!delta || delta->count == 0) {
        return 0;
    }
    uint32_t hash = 0;
    for (size_t i = 0; i < delta->count; ++i) {
        const fkv_delta_entry_t *entry = &delta->entries[i];
        for (size_t j = 0; j < entry->key_len; ++j) {
            hash = hash * 131u + (uint32_t)entry->key[j];
        }
        for (size_t j = 0; j < entry->value_len; ++j) {
            hash = hash * 131u + (uint32_t)entry->value[j];
        }
        hash = hash * 131u + (uint32_t)entry->type;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            hash = hash * 131u + (uint32_t)((entry->priority >> shift) & 0xFFu);
        }
    }
    return (uint16_t)(hash % 65521u);
}

int fkv_export_delta(uint64_t since_sequence, fkv_delta_t *delta) {
    if (!delta) {
        return -1;
    }
    memset(delta, 0, sizeof(*delta));
    delta->min_sequence = UINT64_MAX;
    delta->max_sequence = since_sequence;

    pthread_mutex_lock(&fkv_lock);
    if (!fkv_root) {
        pthread_mutex_unlock(&fkv_lock);
        delta->min_sequence = 0;
        delta->checksum = 0;
        return 0;
    }
    int rc = fkv_collect_delta_entries(fkv_root, since_sequence, delta);
    pthread_mutex_unlock(&fkv_lock);

    if (rc != 0) {
        fkv_delta_free(delta);
        return -1;
    }

    if (delta->count == 0) {
        delta->min_sequence = since_sequence;
        delta->max_sequence = since_sequence;
        delta->checksum = 0;
    } else {
        delta->checksum = fkv_delta_compute_checksum(delta);
    }

    return 0;
}

int fkv_apply_delta(const fkv_delta_t *delta) {
    if (!delta) {
        return -1;
    }
    if (delta->count == 0) {
        return 0;
    }
    uint16_t checksum = fkv_delta_compute_checksum(delta);
    if (checksum != delta->checksum) {
        return -1;
    }
    for (size_t i = 0; i < delta->count; ++i) {
        const fkv_delta_entry_t *entry = &delta->entries[i];
        if (!entry->key || entry->key_len == 0 || !entry->value || entry->value_len == 0) {
            return -1;
        }
        if (fkv_put_scored(entry->key,
                           entry->key_len,
                           entry->value,
                           entry->value_len,
                           entry->type,
                           entry->priority) != 0) {
            return -1;
        }
    }
    return 0;
}

void fkv_delta_free(fkv_delta_t *delta) {
    if (!delta) {
        return;
    }
    for (size_t i = 0; i < delta->count; ++i) {
        fkv_delta_entry_cleanup(&delta->entries[i]);
    }
    free(delta->entries);
    delta->entries = NULL;
    delta->count = 0;
    delta->capacity = 0;
    delta->min_sequence = 0;
    delta->max_sequence = 0;
    delta->total_bytes = 0;
    delta->checksum = 0;
}
