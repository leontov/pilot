#include "node_brain.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <json-c/json.h>
#include <ctype.h>

static void safe_strncpy(char *dst, const char *src, size_t n) {
    if (!dst) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, n - 1);
    dst[n-1] = '\0';
}

int node_brain_init(NodeBrain *nb, const char *storage_prefix) {
    if (!nb) return -1;
    memset(nb, 0, sizeof(*nb));
    nb->energy_budget = 1.0;

    // Try to load memory file: <prefix>_memory.json
    if (storage_prefix) {
        char path[512];
        snprintf(path, sizeof(path), "%s_memory.json", storage_prefix);
        json_object *root = json_object_from_file(path);
        if (root && json_object_is_type(root, json_type_array)) {
            size_t n = json_object_array_length(root);
            for (size_t i = 0; i < n && (int)nb->count < NB_MAX_MEM_ITEMS; ++i) {
                json_object *ent = json_object_array_get_idx(root, i);
                json_object *jk = NULL, *jv = NULL, *jts = NULL;
                json_object_object_get_ex(ent, "key", &jk);
                json_object_object_get_ex(ent, "value", &jv);
                json_object_object_get_ex(ent, "ts", &jts);
                if (jk && jv) {
                    safe_strncpy(nb->items[nb->count].key, json_object_get_string(jk), sizeof(nb->items[nb->count].key));
                    safe_strncpy(nb->items[nb->count].value, json_object_get_string(jv), sizeof(nb->items[nb->count].value));
                    nb->items[nb->count].ts = jts ? (time_t)json_object_get_int64(jts) : time(NULL);
                    nb->count++;
                }
            }
            json_object_put(root);
        }
    }
    return 0;
}

void node_brain_free(NodeBrain *nb) {
    (void)nb;
}

int node_brain_add_memory(NodeBrain *nb, const char *key, const char *value) {
    if (!nb || !key || !value) return -1;
    if (nb->count >= NB_MAX_MEM_ITEMS) return -2;
    safe_strncpy(nb->items[nb->count].key, key, sizeof(nb->items[nb->count].key));
    safe_strncpy(nb->items[nb->count].value, value, sizeof(nb->items[nb->count].value));
    nb->items[nb->count].ts = time(NULL);
    nb->count++;
    return 0;
}

const char* node_brain_get_memory(NodeBrain *nb, const char *key) {
    if (!nb || !key) return NULL;
    for (int i = nb->count - 1; i >= 0; --i) {
        if (strcmp(nb->items[i].key, key) == 0) return nb->items[i].value;
    }
    return NULL;
}

void node_brain_update_numeric(NodeBrain *nb, const double *features, int n) {
    if (!nb || !features) return;
    int m = sizeof(nb->numeric_state)/sizeof(nb->numeric_state[0]);
    for (int i = 0; i < m && i < n; ++i) {
        // simple EMA update
        nb->numeric_state[i] = nb->numeric_state[i] * 0.9 + features[i] * 0.1;
    }
}

static void make_short_reply(const char *task, char *out, size_t out_size) {
    // Very small heuristic reply: echo + minimal transform
    snprintf(out, out_size, "Ответ узла: %s", task);
}

const char* node_brain_process(NodeBrain *nb, const char *task, char *out, size_t out_size) {
    if (!nb || !task || !out) return NULL;
    // check energy budget
    if (nb->energy_budget < 0.05) {
        snprintf(out, out_size, "Нехватка энергии у узла: ответ отложен");
        return out;
    }

    // trivial rule: if task contains 'запомни:' store after colon
    const char *p = strstr(task, "запомни:");
    if (p) {
        p += strlen("запомни:");
        while (*p == ' ') p++;
        char key[128];
        snprintf(key, sizeof(key), "mem_%ld", time(NULL));
        node_brain_add_memory(nb, key, p);
        snprintf(out, out_size, "Сохранено в памяти: %s", key);
        // small energy cost
        nb->energy_budget -= 0.01;
        if (nb->energy_budget < 0) nb->energy_budget = 0;
        return out;
    }

    // Try to find any key mentioned as 'вспомни KEY'
    p = strstr(task, "вспомни ");
    if (p) {
        p += strlen("вспомни ");
        char key[128];
        size_t ki = 0;
        while (*p && !isspace((unsigned char)*p) && ki + 1 < sizeof(key)) key[ki++] = *p++;
        key[ki] = '\0';
        const char *val = node_brain_get_memory(nb, key);
        if (val) snprintf(out, out_size, "Память %s: %s", key, val);
        else snprintf(out, out_size, "Не найдено в памяти: %s", key);
        nb->energy_budget -= 0.005;
        if (nb->energy_budget < 0) nb->energy_budget = 0;
        return out;
    }

    // fallback: make short reply and consume little energy
    make_short_reply(task, out, out_size);
    nb->energy_budget -= 0.002;
    if (nb->energy_budget < 0) nb->energy_budget = 0;
    return out;
}

int node_brain_save(NodeBrain *nb, const char *storage_prefix) {
    if (!nb || !storage_prefix) return -1;
    char path[512];
    snprintf(path, sizeof(path), "%s_memory.json", storage_prefix);
    json_object *arr = json_object_new_array();
    for (int i = 0; i < nb->count; ++i) {
        json_object *e = json_object_new_object();
        json_object_object_add(e, "key", json_object_new_string(nb->items[i].key));
        json_object_object_add(e, "value", json_object_new_string(nb->items[i].value));
        json_object_object_add(e, "ts", json_object_new_int64((int64_t)nb->items[i].ts));
        json_object_array_add(arr, e);
    }
    int r = json_object_to_file(path, arr);
    json_object_put(arr);
    return r == 0 ? 0 : -1;
}
