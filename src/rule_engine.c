#include "rule_engine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "embed.h"

static char rules_path[512];
static json_object *rules_array = NULL;
// cache of embeddings for rule expressions
static double (*rules_embs)[64] = NULL;
static size_t rules_emb_count = 0;

int rule_engine_init(RuleEngine *re, const char *storage_prefix) {
    if (!re || !storage_prefix) return -1;
    strncpy(re->storage_prefix, storage_prefix, sizeof(re->storage_prefix)-1);
    re->storage_prefix[sizeof(re->storage_prefix)-1] = '\0';
    snprintf(rules_path, sizeof(rules_path), "%s_rules.json", storage_prefix);
    rules_array = json_object_from_file(rules_path);
    if (!rules_array) {
        rules_array = json_object_new_array();
        json_object_to_file(rules_path, rules_array);
    }
    // prepare embedding cache
    rules_emb_count = json_object_array_length(rules_array);
    if (rules_emb_count > 0) {
        rules_embs = malloc(sizeof(double[64]) * rules_emb_count);
        for (size_t i = 0; i < rules_emb_count; ++i) {
            json_object *r = json_object_array_get_idx(rules_array, i);
            json_object *jexpr = NULL;
            if (json_object_object_get_ex(r, "expr", &jexpr)) {
                const char *expr = json_object_get_string(jexpr);
                embed_text(expr, rules_embs[i], 64);
            } else {
                for (int k=0;k<64;++k) rules_embs[i][k] = 0.0;
            }
        }
    }
    return 0;
}

void rule_engine_free(RuleEngine *re) {
    (void)re;
    if (rules_array) json_object_put(rules_array);
    rules_array = NULL;
}

int rule_engine_add(RuleEngine *re, json_object *rule) {
    (void)re;
    if (!rule) return -1;
    // ensure basic fields
    if (!json_object_object_get(rule, "id")) {
        char idbuf[64];
        snprintf(idbuf, sizeof(idbuf), "r_%ld", time(NULL));
        json_object_object_add(rule, "id", json_object_new_string(idbuf));
    }
    if (!json_object_object_get(rule, "created_at")) {
        json_object_object_add(rule, "created_at", json_object_new_int64((int64_t)time(NULL)));
    }
    if (!json_object_object_get(rule, "hits")) json_object_object_add(rule, "hits", json_object_new_int(0));
    if (!json_object_object_get(rule, "successes")) json_object_object_add(rule, "successes", json_object_new_int(0));
    if (!json_object_object_get(rule, "score")) json_object_object_add(rule, "score", json_object_new_double(0.0));

    json_object_array_add(rules_array, json_object_get(rule));
    // update cache
    size_t n = json_object_array_length(rules_array);
    rules_embs = realloc(rules_embs, sizeof(double[64]) * n);
    json_object *r = json_object_array_get_idx(rules_array, n-1);
    json_object *jexpr = NULL;
    if (json_object_object_get_ex(r, "expr", &jexpr)) {
        const char *expr = json_object_get_string(jexpr);
        embed_text(expr, rules_embs[n-1], 64);
    } else {
        for (int k=0;k<64;++k) rules_embs[n-1][k] = 0.0;
    }
    return json_object_to_file(rules_path, rules_array) == 0 ? 0 : -1;
}

json_object* rule_engine_list(RuleEngine *re) {
    (void)re;
    if (!rules_array) rules_array = json_object_new_array();
    return json_object_get(rules_array);
}

int rule_engine_save(RuleEngine *re) {
    (void)re;
    if (!rules_array) return -1;
    return json_object_to_file(rules_path, rules_array) == 0 ? 0 : -1;
}

// simple helper: compute a naive match score between task and rule expression
static double compute_match_score(const char *task, const char *expr) {
    if (!task || !expr) return 0.0;
    // prefer exact substring matches, length and token overlap
    if (strstr(task, expr)) return 1.0;
    size_t lt = strlen(task);
    size_t le = strlen(expr);
    if (lt == 0 || le == 0) return 0.0;
    // token overlap
    int common = 0;
    char *tcopy = strdup(task);
    char *tok;
    for (tok = strtok(tcopy, " \t,.;:/"); tok; tok = strtok(NULL, " \t,.;:/")) {
        if (strstr(expr, tok)) common++;
    }
    free(tcopy);
    double token_score = (double)common / (double)(le < 1 ? 1 : le);
    if (token_score > 1.0) token_score = 1.0;

    // embedding similarity fallback (if embed available)
    const size_t EMB_DIM = 64;
    double emb_t[EMB_DIM];
    double emb_e[EMB_DIM];
    // Try embedding similarity (embed_text writes into arrays; embed may be a no-op if not initialized)
    embed_text(task, emb_t, EMB_DIM);
    embed_text(expr, emb_e, EMB_DIM);
    double dot = 0.0, nt = 0.0, ne = 0.0;
    for (size_t i = 0; i < EMB_DIM; ++i) { dot += emb_t[i] * emb_e[i]; nt += emb_t[i] * emb_t[i]; ne += emb_e[i] * emb_e[i]; }
    if (nt > 0.0 && ne > 0.0) {
        double cos = dot / (sqrt(nt) * sqrt(ne));
        if (cos > 1.0) cos = 1.0;
        if (cos < -1.0) cos = -1.0;
        // combine token and embedding scores: prefer exact token overlap but allow embeddings to rescue
        double combined = fmax(token_score, 0.0) * 0.4 + (cos > 0.0 ? cos : 0.0) * 0.6;
        if (combined > 1.0) combined = 1.0;
        return combined;
    }

    return token_score;
}

json_object* rule_engine_find_best_match(RuleEngine *re, const char *task) {
    (void)re;
    if (!rules_array || !task) return NULL;
    double best = 0.0;
    json_object *best_rule = NULL;
    size_t n = json_object_array_length(rules_array);
    double emb_t[64]; embed_text(task, emb_t, 64);
    for (size_t i = 0; i < n; ++i) {
        json_object *r = json_object_array_get_idx(rules_array, i);
        json_object *jexpr = NULL;
        if (!json_object_object_get_ex(r, "expr", &jexpr)) continue;
        const char *expr = json_object_get_string(jexpr);
        double sc = compute_match_score(task, expr);
        // consider rule score as multiplier if present
        json_object *jscore = NULL;
        double rulscore = 0.0;
        if (json_object_object_get_ex(r, "score", &jscore)) rulscore = json_object_get_double(jscore);
        double combined = sc * (1.0 + rulscore);
        if (combined > best) {
            best = combined;
            best_rule = r;
        }
    }
    if (!best_rule) return NULL;
    return json_object_get(best_rule);
}

static int recompute_rule_score(json_object *r) {
    if (!r) return -1;
    json_object *jh = NULL, *js = NULL;
    int hits = 0, succ = 0;
    if (json_object_object_get_ex(r, "hits", &jh)) hits = json_object_get_int(jh);
    if (json_object_object_get_ex(r, "successes", &js)) succ = json_object_get_int(js);
    double rate = 0.0;
    if (hits > 0) rate = ((double)succ + 1.0) / ((double)hits + 2.0); // Laplace smoothing
    double score = rate * log2(1.0 + (double)hits);
    // complexity penalty if expr too long
    json_object *jexpr = NULL;
    if (json_object_object_get_ex(r, "expr", &jexpr)) {
        const char *expr = json_object_get_string(jexpr);
        size_t len = strlen(expr);
        double pen = 1.0 - fmin(0.5, (double)len / 200.0);
        score *= pen;
    }
    json_object_object_add(r, "score", json_object_new_double(score));
    return 0;
}

int rule_engine_record_hit(RuleEngine *re, const char *rule_id) {
    (void)re;
    if (!rules_array || !rule_id) return -1;
    size_t n = json_object_array_length(rules_array);
    for (size_t i = 0; i < n; ++i) {
        json_object *r = json_object_array_get_idx(rules_array, i);
        json_object *jid = NULL;
        if (!json_object_object_get_ex(r, "id", &jid)) continue;
        if (strcmp(json_object_get_string(jid), rule_id) == 0) {
            json_object *jh = NULL;
            int hits = 0;
            if (json_object_object_get_ex(r, "hits", &jh)) hits = json_object_get_int(jh);
            json_object_object_add(r, "hits", json_object_new_int(hits + 1));
            recompute_rule_score(r);
            json_object_to_file(rules_path, rules_array);
            return 0;
        }
    }
    return -1;
}

int rule_engine_record_success(RuleEngine *re, const char *rule_id) {
    (void)re;
    if (!rules_array || !rule_id) return -1;
    size_t n = json_object_array_length(rules_array);
    for (size_t i = 0; i < n; ++i) {
        json_object *r = json_object_array_get_idx(rules_array, i);
        json_object *jid = NULL;
        if (!json_object_object_get_ex(r, "id", &jid)) continue;
        if (strcmp(json_object_get_string(jid), rule_id) == 0) {
            json_object *js = NULL;
            int succ = 0;
            if (json_object_object_get_ex(r, "successes", &js)) succ = json_object_get_int(js);
            json_object_object_add(r, "successes", json_object_new_int(succ + 1));
            recompute_rule_score(r);
            json_object_to_file(rules_path, rules_array);
            return 0;
        }
    }
    return -1;
}

int rule_engine_gc(RuleEngine *re, int min_hits, double min_success_rate) {
    (void)re;
    if (!rules_array) return 0;
    json_object *new_arr = json_object_new_array();
    size_t n = json_object_array_length(rules_array);
    int removed = 0;
    for (size_t i = 0; i < n; ++i) {
        json_object *r = json_object_array_get_idx(rules_array, i);
        json_object *jh = NULL, *js = NULL;
        int hits = 0, succ = 0;
        if (json_object_object_get_ex(r, "hits", &jh)) hits = json_object_get_int(jh);
        if (json_object_object_get_ex(r, "successes", &js)) succ = json_object_get_int(js);
        double rate = (hits > 0) ? ((double)succ / (double)hits) : 0.0;
        if (hits >= min_hits && rate < min_success_rate) {
            removed++;
            continue; // prune
        }
        json_object_array_add(new_arr, json_object_get(r));
    }
    if (rules_array) json_object_put(rules_array);
    rules_array = new_arr;
    json_object_to_file(rules_path, rules_array);
    return removed;
}

int rule_engine_sync(RuleEngine *re, const char *cluster_path) {
    if (!re || !cluster_path) return -1;
    json_object *cluster = json_object_from_file(cluster_path);
    if (!cluster || json_object_get_type(cluster) != json_type_array) { if (cluster) json_object_put(cluster); return -1; }
    int added = 0;
    size_t n = json_object_array_length(cluster);
    for (size_t i = 0; i < n; ++i) {
        json_object *r = json_object_array_get_idx(cluster, i);
        json_object *jexpr = NULL, *jid = NULL;
        if (!json_object_object_get_ex(r, "expr", &jexpr)) continue;
        const char *expr = json_object_get_string(jexpr);
        // check if expr exists in local rules
        int exists = 0;
        size_t m = json_object_array_length(rules_array);
        for (size_t j = 0; j < m; ++j) {
            json_object *lr = json_object_array_get_idx(rules_array, j);
            json_object *lexpr = NULL;
            if (json_object_object_get_ex(lr, "expr", &lexpr)) {
                if (strcmp(expr, json_object_get_string(lexpr)) == 0) { exists = 1; break; }
            }
        }
        if (!exists) {
            json_object_array_add(rules_array, json_object_get(r));
            added++;
        }
    }
    if (added > 0) json_object_to_file(rules_path, rules_array);
    json_object_put(cluster);
    return added;
}
