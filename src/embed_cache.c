// Minimal thread-safe LRU cache implementation for embeddings
#include "embed_cache.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>

static unsigned long simple_hash(const char *s) {
    unsigned long h = 5381;
    while (*s) h = ((h << 5) + h) + (unsigned char)(*s++);
    return h;

}

EmbCache *emb_cache_create(size_t capacity, unsigned int dim) {
    EmbCache *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->table_size = capacity * 2 + 1;
    c->table = calloc(c->table_size, sizeof(EmbEntry*));
    c->capacity = capacity;
    c->dim = dim;
    pthread_mutex_init(&c->lock, NULL);
    return c;
}

static void detach(EmbCache *c, EmbEntry *e) {
    if (!e) return;
    if (e->prev) e->prev->next = e->next; else c->head = e->next;
    if (e->next) e->next->prev = e->prev; else c->tail = e->prev;
    e->prev = e->next = NULL;
}

static void attach_head(EmbCache *c, EmbEntry *e) {
    e->next = c->head;
    e->prev = NULL;
    if (c->head) c->head->prev = e;
    c->head = e;
    if (!c->tail) c->tail = e;
}

void emb_cache_destroy(EmbCache *c) {
    if (!c) return;
    for (size_t i = 0; i < c->table_size; ++i) {
        EmbEntry *e = c->table[i];
        while (e) {
            EmbEntry *n = e->next;
            free(e->vec);
            free(e);
            e = n;
        }
    }
    free(c->table);
    // free any list nodes (already freed above)
    pthread_mutex_destroy(&c->lock);
    free(c);
}

float *emb_cache_get(EmbCache *c, const char *key) {
    if (!c || !key) return NULL;
    unsigned long h = simple_hash(key) % c->table_size;
    pthread_mutex_lock(&c->lock);
    EmbEntry *e = c->table[h];
    while (e) {
        if (strncmp(e->key, key, EMB_CACHE_KEY_LEN) == 0) {
            // move to head
            detach(c, e);
            attach_head(c, e);
            pthread_mutex_unlock(&c->lock);
            return e->vec;
        }
        e = e->next;
    }
    pthread_mutex_unlock(&c->lock);
    return NULL;
}

void emb_cache_put(EmbCache *c, const char *key, const float *vec) {
    if (!c || !key || !vec) return;
    unsigned long h = simple_hash(key) % c->table_size;
    pthread_mutex_lock(&c->lock);
    EmbEntry *e = c->table[h];
    while (e) {
        if (strncmp(e->key, key, EMB_CACHE_KEY_LEN) == 0) {
            // update vector
            if (e->vec) free(e->vec);
            e->vec = malloc(sizeof(float) * c->dim);
            memcpy(e->vec, vec, sizeof(float) * c->dim);
            // move to head
            detach(c, e);
            attach_head(c, e);
            pthread_mutex_unlock(&c->lock);
            return;
        }
        e = e->next;
    }
    // create new entry
    EmbEntry *ne = calloc(1, sizeof(*ne));
    strncpy(ne->key, key, EMB_CACHE_KEY_LEN-1);
    ne->vec = malloc(sizeof(float) * c->dim);
    memcpy(ne->vec, vec, sizeof(float) * c->dim);
    ne->dim = c->dim;
    // insert into hash bucket (simple chaining)
    ne->next = c->table[h];
    if (c->table[h]) c->table[h]->prev = ne; // chain prev used only for LRU
    c->table[h] = ne;
    attach_head(c, ne);
    c->size += 1;
    // evict if needed
    if (c->size > c->capacity) {
        // remove tail
        EmbEntry *ev = c->tail;
        if (ev) {
            // remove from its hash bucket
            unsigned long hh = simple_hash(ev->key) % c->table_size;
            EmbEntry *iter = c->table[hh];
            EmbEntry *prev = NULL;
            while (iter) {
                if (iter == ev) {
                    if (prev) prev->next = iter->next; else c->table[hh] = iter->next;
                    break;
                }
                prev = iter;
                iter = iter->next;
            }
            detach(c, ev);
            free(ev->vec);
            free(ev);
            c->size -= 1;
        }
    }
    pthread_mutex_unlock(&c->lock);
}
