#define _POSIX_C_SOURCE 200809L
#include "http/http_routes.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "formula_core.h"

static uint64_t routes_start_time = 0;
static Blockchain *routes_blockchain = NULL;

typedef struct {
    char *program_id;
} submitted_program_t;

static submitted_program_t *submitted_programs = NULL;
static size_t submitted_program_count = 0;
static size_t submitted_program_capacity = 0;
static size_t next_program_id = 1;

static char *duplicate_string(const char *src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}

static int respond_json(http_response_t *resp, const char *json, int status) {
    if (!resp || !json) {
        return -1;
    }
    char *data = duplicate_string(json);
    if (!data) {
        return -1;
    }
    resp->data = data;
    resp->len = strlen(data);
    resp->status = status;
    snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
    return 0;
}

static void remember_program_id(const char *program_id) {
    if (!program_id) {
        return;
    }
    if (submitted_program_count == submitted_program_capacity) {
        size_t new_capacity = submitted_program_capacity ? submitted_program_capacity * 2 : 8;
        submitted_program_t *tmp = realloc(submitted_programs, new_capacity * sizeof(*tmp));
        if (!tmp) {
            return;
        }
        submitted_programs = tmp;
        for (size_t i = submitted_program_capacity; i < new_capacity; ++i) {
            submitted_programs[i].program_id = NULL;
        }
        submitted_program_capacity = new_capacity;
    }
    char *copy = duplicate_string(program_id);
    if (!copy) {
        return;
    }
    submitted_programs[submitted_program_count++].program_id = copy;
}

static int program_was_submitted(const char *program_id) {
    if (!program_id) {
        return 0;
    }
    for (size_t i = 0; i < submitted_program_count; ++i) {
        if (submitted_programs[i].program_id && strcmp(submitted_programs[i].program_id, program_id) == 0) {
            return 1;
        }
    }
    return 0;
}

static void free_submitted_programs(void) {
    for (size_t i = 0; i < submitted_program_count; ++i) {
        free(submitted_programs[i].program_id);
        submitted_programs[i].program_id = NULL;
    }
    free(submitted_programs);
    submitted_programs = NULL;
    submitted_program_count = 0;
    submitted_program_capacity = 0;
}

static int parse_bytecode_count(const char *body, size_t body_len, size_t *out_count) {
    if (!body || !out_count) {
        return -1;
    }
    if (body_len == 0) {
        body_len = strlen(body);
    }
    const char *key_pos = strstr(body, "\"bytecode\"");
    if (!key_pos) {
        return -1;
    }
    const char *start = strchr(key_pos, '[');
    if (!start) {
        return -1;
    }
    const char *end = strchr(start, ']');
    if (!end || end <= start) {
        return -1;
    }
    size_t count = 0;
    const char *ptr = start + 1;
    while (ptr < end) {
        while (ptr < end && isspace((unsigned char)*ptr)) {
            ++ptr;
        }
        if (ptr >= end) {
            break;
        }
        if (*ptr == ',') {
            ++ptr;
            continue;
        }
        if (!isdigit((unsigned char)*ptr)) {
            return -1;
        }
        while (ptr < end && isdigit((unsigned char)*ptr)) {
            ++ptr;
        }
        ++count;
        while (ptr < end && isspace((unsigned char)*ptr)) {
            ++ptr;
        }
        if (ptr < end && *ptr == ',') {
            ++ptr;
        }
    }
    if (count == 0) {
        return -1;
    }
    *out_count = count;
    return 0;
}

static int extract_string_field(const char *body, size_t body_len, const char *key, char *out, size_t out_len) {
    if (!body || !key || !out || out_len == 0) {
        return -1;
    }
    if (body_len == 0) {
        body_len = strlen(body);
    }
    const char *key_pos = strstr(body, key);
    if (!key_pos) {
        return -1;
    }
    const char *colon = strchr(key_pos, ':');
    if (!colon) {
        return -1;
    }
    const char *ptr = colon + 1;
    while (*ptr && isspace((unsigned char)*ptr)) {
        ++ptr;
    }
    if (*ptr != '"') {
        return -1;
    }
    ++ptr;
    const char *end = strchr(ptr, '"');
    if (!end) {
        return -1;
    }
    size_t len = (size_t)(end - ptr);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, ptr, len);
    out[len] = '\0';
    return 0;
}

static int handle_health(http_response_t *resp) {
    uint64_t uptime_ms = routes_start_time ? (now_ms() - routes_start_time) : 0;
    char buffer[256];
    int written = snprintf(buffer,
                           sizeof(buffer),
                           "{\"uptime_ms\":%llu,\"blockchain_attached\":%s}",
                           (unsigned long long)uptime_ms,
                           routes_blockchain ? "true" : "false");
    if (written < 0) {
        return -1;
    }
    return respond_json(resp, buffer, 200);
}

static int handle_metrics(http_response_t *resp) {
    const char *json = "{\"requests\":0,\"errors\":0}";
    return respond_json(resp, json, 200);
}

static int handle_dialog(const char *body, size_t body_len, http_response_t *resp) {
    (void)body_len;
    const char *reply = "{\"answer\":\"Kolibri is online\"}";
    if (body && strstr(body, "ping")) {
        reply = "{\"answer\":\"pong\"}";
    }
    return respond_json(resp, reply, 200);
}

static int handle_program_submit(const char *body, size_t body_len, http_response_t *resp) {
    if (!routes_blockchain) {
        return respond_json(resp, "{\"error\":\"blockchain_unavailable\"}", 503);
    }
    size_t program_len = 0;
    if (parse_bytecode_count(body, body_len, &program_len) != 0) {
        return respond_json(resp, "{\"error\":\"invalid_program\"}", 400);
    }

    Formula formula;
    memset(&formula, 0, sizeof(formula));
    snprintf(formula.id, sizeof(formula.id), "program-%zu", next_program_id);
    formula.effectiveness = program_len > 0 ? 0.5 + 0.5 * ((double)program_len / (double)(program_len + 10)) : 0.5;
    formula.created_at = time(NULL);
    formula.representation = FORMULA_REPRESENTATION_TEXT;
    formula.type = FORMULA_LINEAR;
    snprintf(formula.content, sizeof(formula.content), "bytecode:%zu", program_len);

    Formula *formulas[1] = {&formula};
    bool added = blockchain_add_block(routes_blockchain, formulas, 1);
    if (!added) {
        return respond_json(resp, "{\"error\":\"blockchain_rejected\"}", 500);
    }

    double poe = 0.0;
    double mdl = 0.0;
    double score = blockchain_score_formula(&formula, &poe, &mdl);

    char response[256];
    int written = snprintf(response,
                           sizeof(response),
                           "{\"program_id\":\"%s\",\"PoE\":%.3f,\"MDL\":%.3f,\"score\":%.3f}",
                           formula.id,
                           poe,
                           mdl,
                           score);
    if (written < 0) {
        return respond_json(resp, "{\"error\":\"internal_error\"}", 500);
    }

    remember_program_id(formula.id);
    next_program_id++;
    return respond_json(resp, response, 200);
}

static int handle_chain_submit(const char *body, size_t body_len, http_response_t *resp) {
    char program_id[128];
    if (extract_string_field(body, body_len, "\"program_id\"", program_id, sizeof(program_id)) != 0) {
        return respond_json(resp, "{\"status\":\"not_found\"}", 404);
    }

    if (program_was_submitted(program_id)) {
        char response[256];
        snprintf(response,
                 sizeof(response),
                 "{\"status\":\"accepted\",\"program_id\":\"%s\"}",
                 program_id);
        return respond_json(resp, response, 200);
    }

    return respond_json(resp, "{\"status\":\"not_found\"}", 404);
}

int http_handle_request(const kolibri_config_t *cfg,
                        const char *method,
                        const char *path,
                        const char *body,
                        size_t body_len,
                        http_response_t *resp) {
    (void)cfg;
    if (!method || !path || !resp) {
        return -1;
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/health") == 0) {
        return handle_health(resp);
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/metrics") == 0) {
        return handle_metrics(resp);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/dialog") == 0) {
        return handle_dialog(body, body_len, resp);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/program/submit") == 0) {
        return handle_program_submit(body, body_len, resp);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/chain/submit") == 0) {
        return handle_chain_submit(body, body_len, resp);
    }
    const char *not_found = "{\"error\":\"not_found\"}";
    return respond_json(resp, not_found, 404);
}

void http_response_free(http_response_t *resp) {
    if (!resp) {
        return;
    }
    free(resp->data);
    resp->data = NULL;
    resp->len = 0;
    resp->status = 0;
    resp->content_type[0] = '\0';
}

void http_routes_set_start_time(uint64_t ms_since_epoch) {
    routes_start_time = ms_since_epoch;
}

void http_routes_set_blockchain(Blockchain *chain) {
    if (!chain) {
        free_submitted_programs();
        next_program_id = 1;
    }
    routes_blockchain = chain;
}
