// Simple thread-safe LRU cache for embeddings
#pragma once
#include <stddef.h>
#include <pthread.h>

#define EMB_CACHE_KEY_LEN 128

typedef struct EmbEntry {
    char key[EMB_CACHE_KEY_LEN];
    float *vec; // owns float array of length dim
    size_t dim;
    struct EmbEntry *prev, *next; // for LRU doubly-linked list
} EmbEntry;

typedef struct EmbCache {
    EmbEntry **table; // simple hash table
    size_t table_size;
    EmbEntry *head, *tail; // LRU list: head = most recent
    size_t capacity;
    size_t size;
    unsigned int dim;
    pthread_mutex_t lock;
} EmbCache;

// Create/destroy
EmbCache *emb_cache_create(size_t capacity, unsigned int dim);
void emb_cache_destroy(EmbCache *c);

// Lookup (returns pointer to vector owned by cache or NULL)
float *emb_cache_get(EmbCache *c, const char *key);
// Insert (copies vector into cache). If evicted, frees old vector.
void emb_cache_put(EmbCache *c, const char *key, const float *vec);
