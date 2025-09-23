#ifndef NODE_BRAIN_H
#define NODE_BRAIN_H

#include <time.h>

#define NB_MAX_MEM_ITEMS 1024
#define NB_MEM_ITEM_SIZE 1024

typedef struct {
    char key[128];
    char value[NB_MEM_ITEM_SIZE];
    time_t ts;
} NB_MemoryItem;

typedef struct {
    NB_MemoryItem items[NB_MAX_MEM_ITEMS];
    int count;
    double numeric_state[16]; // small numeric state vector
    double energy_budget; // 0..1
} NodeBrain;

// Initialize brain; loads memory from file if present
int node_brain_init(NodeBrain *nb, const char *storage_prefix);
// Free resources (if any)
void node_brain_free(NodeBrain *nb);
// Add a memory entry (append)
int node_brain_add_memory(NodeBrain *nb, const char *key, const char *value);
// Simple retrieval by key (returns NULL if not found)
const char* node_brain_get_memory(NodeBrain *nb, const char *key);
// Update numeric state (in-place)
void node_brain_update_numeric(NodeBrain *nb, const double *features, int n);
// Produce an answer based on task string, outputs into out buffer
const char* node_brain_process(NodeBrain *nb, const char *task, char *out, size_t out_size);
// Save persistent state (memory + numeric_state) to disk
int node_brain_save(NodeBrain *nb, const char *storage_prefix);

#endif // NODE_BRAIN_H
