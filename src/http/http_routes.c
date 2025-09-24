


/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */
#define _POSIX_C_SOURCE 200809L
#include "http/http_routes.h"

#include "blockchain.h"
#include "fkv/fkv.h"
#include "kolibri_ai.h"
#include "synthesis/formula_vm_eval.h"

#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t routes_start_time = 0;
static Blockchain *routes_blockchain = NULL;
static KolibriAI *routes_ai = NULL;

typedef struct {
    Formula formula;
    double poe;
    double mdl;
    double score;
} submitted_program_t;

static submitted_program_t *submitted_programs = NULL;
static size_t submitted_program_count = 0;
static size_t submitted_program_capacity = 0;
static uint64_t submitted_program_counter = 0;

static char *duplicate_string(const char *src) {
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

static int respond_error(http_response_t *resp, int status, const char *code, const char *message) {
    if (!code) {
        code = "internal_error";
    }
    if (!message) {
        message = "internal error";
    }
    char buffer[256];
    int written = snprintf(buffer,
                           sizeof(buffer),
                           "{\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}",
                           code,
                           message);
    if (written < 0) {
        return -1;
    }
    return respond_json(resp, buffer, status);
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} json_buffer_t;

static int json_buffer_reserve(json_buffer_t *buf, size_t extra) {
    if (!buf) {
        return -1;
    }
    size_t needed = buf->len + extra + 1;
    if (needed <= buf->cap) {
        return 0;
    }
    size_t new_cap = buf->cap ? buf->cap : 128;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    char *tmp = realloc(buf->data, new_cap);
    if (!tmp) {
        return -1;
    }
    buf->data = tmp;
    buf->cap = new_cap;
    return 0;
}

static int json_buffer_append(json_buffer_t *buf, const char *text) {
    if (!buf || !text) {
        return -1;
    }
    size_t len = strlen(text);
    if (json_buffer_reserve(buf, len) != 0) {
        return -1;
    }
    memcpy(buf->data + buf->len, text, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return 0;
}

static int json_buffer_append_escaped(json_buffer_t *buf, const char *text, size_t len) {
    static const char hex[] = "0123456789ABCDEF";
    if (!buf || !text) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') {
            if (json_buffer_reserve(buf, 2) != 0) {
                return -1;
            }
            buf->data[buf->len++] = '\\';
            buf->data[buf->len++] = (char)c;
        } else if (c < 0x20) {
            if (json_buffer_reserve(buf, 6) != 0) {
                return -1;
            }
            buf->data[buf->len++] = '\\';
            buf->data[buf->len++] = 'u';
            buf->data[buf->len++] = '0';
            buf->data[buf->len++] = '0';
            buf->data[buf->len++] = hex[(c >> 4) & 0xF];
            buf->data[buf->len++] = hex[c & 0xF];
        } else {
            if (json_buffer_reserve(buf, 1) != 0) {
                return -1;
            }
            buf->data[buf->len++] = (char)c;
        }
    }
    if (json_buffer_reserve(buf, 0) != 0) {
        return -1;
    }
    buf->data[buf->len] = '\0';
    return 0;
}

static const char *find_json_key(const char *json, const char *key) {
    if (!json || !key) {
        return NULL;
    }
    char pattern[64];
    size_t key_len = strlen(key);
    if (key_len + 3 >= sizeof(pattern)) {
        return NULL;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(json, pattern);
}

static int json_extract_string(const char *json, const char *key, char *out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) {
        return -1;
    }
    const char *pos = find_json_key(json, key);
    if (!pos) {
        return -1;
    }
    pos += strlen(key) + 2; /* skip "key" */
    const char *colon = strchr(pos, ':');
    if (!colon) {
        return -1;
    }
    const char *start = strchr(colon, '"');
    if (!start) {
        return -1;
    }
    start++;
    const char *end = strchr(start, '"');
    if (!end) {
        return -1;
    }
    size_t len = (size_t)(end - start);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static int json_extract_uint32(const char *json, const char *key, uint32_t *out) {
    if (!json || !key || !out) {
        return -1;
    }
    const char *pos = find_json_key(json, key);
    if (!pos) {
        return -1;
    }
    pos += strlen(key) + 2;
    const char *colon = strchr(pos, ':');
    if (!colon) {
        return -1;
    }
    colon++;
    while (*colon && isspace((unsigned char)*colon)) {
        colon++;
    }
    if (!*colon) {
        return -1;
    }
    char *endptr = NULL;
    unsigned long value = strtoul(colon, &endptr, 10);
    if (colon == endptr) {
        return -1;
    }
    if (value > UINT32_MAX) {
        value = UINT32_MAX;
    }
    *out = (uint32_t)value;
    return 0;
}

static int json_extract_uint8_array(const char *json,
                                    const char *key,
                                    uint8_t **out_data,
                                    size_t *out_len) {
    if (!json || !key || !out_data || !out_len) {
        return -1;
    }
    const char *pos = find_json_key(json, key);
    if (!pos) {
        return -1;
    }
    pos += strlen(key) + 2;
    const char *colon = strchr(pos, ':');
    if (!colon) {
        return -1;
    }
    const char *start = strchr(colon, '[');
    if (!start) {
        return -1;
    }
    start++;
    size_t capacity = 8;
    size_t count = 0;
    uint8_t *data = malloc(capacity);
    if (!data) {
        return -1;
    }
    while (*start) {
        while (*start && isspace((unsigned char)*start)) {
            start++;
        }
        if (*start == ']') {
            start++;
            break;
        }
        char *endptr = NULL;
        unsigned long value = strtoul(start, &endptr, 10);
        if (start == endptr) {
            free(data);
            return -1;
        }
        if (value > 255) {
            free(data);
            return -1;
        }
        if (count >= capacity) {
            size_t new_cap = capacity * 2;
            uint8_t *tmp = realloc(data, new_cap);
            if (!tmp) {
                free(data);
                return -1;
            }
            data = tmp;
            capacity = new_cap;
        }
        data[count++] = (uint8_t)value;
        start = endptr;
        while (*start && isspace((unsigned char)*start)) {
            start++;
        }
        if (*start == ',') {
            start++;
        } else if (*start == ']') {
            start++;
            break;
        }
    }
    if (count == 0) {
        free(data);
        return -1;
    }
    *out_data = data;
    *out_len = count;
    return 0;
}

static int ensure_submitted_capacity(size_t needed) {
    if (needed <= submitted_program_capacity) {
        return 0;
    }
    size_t new_cap = submitted_program_capacity ? submitted_program_capacity : 4;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    submitted_program_t *tmp = realloc(submitted_programs, new_cap * sizeof(*tmp));
    if (!tmp) {
        return -1;
    }
    for (size_t i = submitted_program_capacity; i < new_cap; ++i) {
        memset(&tmp[i], 0, sizeof(tmp[i]));
    }
    submitted_programs = tmp;
    submitted_program_capacity = new_cap;
    return 0;
}

static submitted_program_t *find_submitted_program(const char *program_id) {
    if (!program_id) {
        return NULL;
    }
    for (size_t i = 0; i < submitted_program_count; ++i) {
        if (strcmp(submitted_programs[i].formula.id, program_id) == 0) {
            return &submitted_programs[i];
        }
    }
    return NULL;
}

static int digits_from_string(const char *str, uint8_t *out, size_t *out_len, size_t max_len) {
    if (!str || !out || !out_len) {
        return -1;
    }
    size_t len = strlen(str);
    if (len == 0 || len > max_len) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!isdigit((unsigned char)str[i])) {
            return -1;
        }
        out[i] = (uint8_t)(str[i] - '0');
    }
    *out_len = len;
    return 0;
}

static int digits_to_string(const uint8_t *digits, size_t len, char *out, size_t out_size) {
    if (!digits || !out || out_size == 0) {
        return -1;
    }
    if (len + 1 > out_size) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        if (digits[i] > 9) {
            return -1;
        }
        out[i] = (char)('0' + digits[i]);
    }
    out[len] = '\0';
    return 0;
}

static int handle_vm_run(const kolibri_config_t *cfg,
                         const char *body,
                         http_response_t *resp) {
    if (!cfg || !body) {
        return respond_error(resp, 400, "bad_request", "missing body");
    }
    char program_expr[256];
    program_expr[0] = '\0';
    uint8_t *bytecode = NULL;
    size_t bytecode_len = 0;
    if (json_extract_string(body, "program", program_expr, sizeof(program_expr)) == 0 ||
        json_extract_string(body, "formula", program_expr, sizeof(program_expr)) == 0 ||
        json_extract_string(body, "expression", program_expr, sizeof(program_expr)) == 0) {
        if (formula_vm_compile_from_text(program_expr, &bytecode, &bytecode_len) != 0) {
            return respond_error(resp, 400, "bad_request", "unable to compile program");
        }
    } else if (json_extract_uint8_array(body, "bytecode", &bytecode, &bytecode_len) != 0) {
        return respond_error(resp, 400, "bad_request", "program field missing");
    }

    vm_limits_t limits = {
        .max_steps = cfg->vm.max_steps ? cfg->vm.max_steps : 512,
        .max_stack = cfg->vm.max_stack ? cfg->vm.max_stack : 128,
    };
    uint32_t gas_limit = 0;
    if (json_extract_uint32(body, "gas_limit", &gas_limit) == 0 && gas_limit > 0) {
        limits.max_steps = gas_limit;
    }

    prog_t prog = {.code = bytecode, .len = bytecode_len};
    vm_result_t result = {0};
    int rc = vm_run(&prog, &limits, NULL, &result);
    free(bytecode);
    if (rc != 0 || result.status != VM_OK) {
        return respond_error(resp, 400, "vm_error", "virtual machine rejected program");
    }

    char buffer[256];
    snprintf(buffer,
             sizeof(buffer),
             "{\"result\":\"%llu\",\"stack\":[\"%llu\"],\"trace\":{\"steps\":[]},"
             "\"gas_used\":%u}",
             (unsigned long long)result.result,
             (unsigned long long)result.result,
             result.steps);
    return respond_json(resp, buffer, 200);
}

static int parse_query_param(const char *path, const char *name, char *out, size_t out_size) {
    if (!path || !name || !out || out_size == 0) {
        return -1;
    }
    const char *query = strchr(path, '?');
    if (!query) {
        return -1;
    }
    query++;
    size_t name_len = strlen(name);
    while (*query) {
        if (strncmp(query, name, name_len) == 0 && query[name_len] == '=') {
            query += name_len + 1;
            size_t i = 0;
            while (*query && *query != '&' && i + 1 < out_size) {
                out[i++] = *query++;
            }
            out[i] = '\0';
            return i > 0 ? 0 : -1;
        }
        while (*query && *query != '&') {
            query++;
        }
        if (*query == '&') {
            query++;
        }
    }
    return -1;
}

static int handle_fkv_get(const char *path, http_response_t *resp) {
    if (!path) {
        return respond_error(resp, 400, "bad_request", "missing path");
    }
    char prefix_raw[128];
    if (parse_query_param(path, "prefix", prefix_raw, sizeof(prefix_raw)) != 0) {
        return respond_error(resp, 400, "bad_request", "prefix parameter required");
    }
    uint8_t digits[128];
    size_t digits_len = 0;
    if (digits_from_string(prefix_raw, digits, &digits_len, sizeof(digits)) != 0) {
        return respond_error(resp, 400, "bad_request", "prefix must be decimal digits");
    }
    char limit_raw[32];
    size_t limit = 32;
    if (parse_query_param(path, "limit", limit_raw, sizeof(limit_raw)) == 0) {
        limit = strtoul(limit_raw, NULL, 10);
        if (limit == 0) {
            limit = 1;
        }
    }

    fkv_iter_t iter = {0};
    if (fkv_get_prefix(digits, digits_len, &iter, limit) != 0) {
        return respond_error(resp, 500, "internal_error", "fkv lookup failed");
    }

    json_buffer_t buf = {0};
    int status = -1;
    if (json_buffer_append(&buf, "{\"values\":[") != 0) {
        status = respond_error(resp, 500, "internal_error", "allocation failure");
        goto cleanup;
    }
    int first_value = 1;
    int first_program = 1;
    for (size_t i = 0; i < iter.count; ++i) {
        fkv_entry_t *entry = &iter.entries[i];
        char key_str[128];
        char value_str[256];
        if (digits_to_string(entry->key, entry->key_len, key_str, sizeof(key_str)) != 0) {
            continue;
        }
        if (digits_to_string(entry->value, entry->value_len, value_str, sizeof(value_str)) != 0) {
            continue;
        }
        if (entry->type == FKV_ENTRY_TYPE_VALUE) {
            if (!first_value && json_buffer_append(&buf, ",") != 0) {
                status = respond_error(resp, 500, "internal_error", "allocation failure");
                goto cleanup;
            }
            first_value = 0;
            if (json_buffer_append(&buf, "{\"key\":\"") != 0 ||
                json_buffer_append_escaped(&buf, key_str, strlen(key_str)) != 0 ||
                json_buffer_append(&buf, "\",\"value\":\"") != 0 ||
                json_buffer_append_escaped(&buf, value_str, strlen(value_str)) != 0 ||
                json_buffer_append(&buf, "\"}") != 0) {
                status = respond_error(resp, 500, "internal_error", "allocation failure");
                goto cleanup;
            }
        }
    }
    if (json_buffer_append(&buf, "],\"programs\":[") != 0) {
        status = respond_error(resp, 500, "internal_error", "allocation failure");
        goto cleanup;
    }
    for (size_t i = 0; i < iter.count; ++i) {
        fkv_entry_t *entry = &iter.entries[i];
        if (entry->type != FKV_ENTRY_TYPE_PROGRAM) {
            continue;
        }
        char key_str[128];
        char value_str[256];
        if (digits_to_string(entry->key, entry->key_len, key_str, sizeof(key_str)) != 0) {
            continue;
        }
        if (digits_to_string(entry->value, entry->value_len, value_str, sizeof(value_str)) != 0) {
            continue;
        }
        if (!first_program && json_buffer_append(&buf, ",") != 0) {
            status = respond_error(resp, 500, "internal_error", "allocation failure");
            goto cleanup;
        }
        first_program = 0;
        if (json_buffer_append(&buf, "{\"key\":\"") != 0 ||
            json_buffer_append_escaped(&buf, key_str, strlen(key_str)) != 0 ||
            json_buffer_append(&buf, "\",\"program\":\"") != 0 ||
            json_buffer_append_escaped(&buf, value_str, strlen(value_str)) != 0 ||
            json_buffer_append(&buf, "\"}") != 0) {
            status = respond_error(resp, 500, "internal_error", "allocation failure");
            goto cleanup;
        }
    }
    if (json_buffer_append(&buf, "]}") != 0) {
        status = respond_error(resp, 500, "internal_error", "allocation failure");
        goto cleanup;
    }
    status = respond_json(resp, buf.data ? buf.data : "{\"values\":[],\"programs\":[]}", 200);

cleanup:
    free(buf.data);
    fkv_iter_free(&iter);
    return status;
}

static int handle_program_submit(const kolibri_config_t *cfg,
                                 const char *body,
                                 http_response_t *resp) {
    if (!cfg || !body) {
        return respond_error(resp, 400, "bad_request", "missing body");
    }
    char program_expr[256];
    if (json_extract_string(body, "program", program_expr, sizeof(program_expr)) != 0 &&
        json_extract_string(body, "formula", program_expr, sizeof(program_expr)) != 0 &&
        json_extract_string(body, "content", program_expr, sizeof(program_expr)) != 0) {
        return respond_error(resp, 400, "bad_request", "program field missing");
    }
    uint8_t *bytecode = NULL;
    size_t bytecode_len = 0;
    if (formula_vm_compile_from_text(program_expr, &bytecode, &bytecode_len) != 0) {
        return respond_error(resp, 400, "bad_request", "unable to compile program");
    }

    vm_limits_t limits = {
        .max_steps = cfg->vm.max_steps ? cfg->vm.max_steps : 512,
        .max_stack = cfg->vm.max_stack ? cfg->vm.max_stack : 128,
    };
    prog_t prog = {.code = bytecode, .len = bytecode_len};
    vm_result_t result = {0};
    int vm_rc = vm_run(&prog, &limits, NULL, &result);
    free(bytecode);
    if (vm_rc != 0 || result.status != VM_OK) {
        return respond_error(resp, 400, "vm_error", "virtual machine rejected program");
    }

    if (ensure_submitted_capacity(submitted_program_count + 1) != 0) {
        return respond_error(resp, 500, "internal_error", "allocation failure");
    }

    submitted_program_counter++;
    submitted_program_t *slot = &submitted_programs[submitted_program_count];
    memset(slot, 0, sizeof(*slot));
    snprintf(slot->formula.id,
             sizeof(slot->formula.id),
             "prog-%06llu",
             (unsigned long long)submitted_program_counter);
    slot->formula.representation = FORMULA_REPRESENTATION_TEXT;
    slot->formula.type = FORMULA_LINEAR;
    slot->formula.created_at = time(NULL);
    slot->formula.tests_passed = 1;
    slot->formula.confirmations = 0;
    slot->formula.effectiveness = result.halted ? 1.0 : 0.0;
    strncpy(slot->formula.content, program_expr, sizeof(slot->formula.content) - 1);
    slot->formula.content[sizeof(slot->formula.content) - 1] = '\0';

    double poe = 0.0;
    double mdl = 0.0;
    slot->score = blockchain_score_formula(&slot->formula, &poe, &mdl);
    slot->poe = poe;
    slot->mdl = mdl;
    submitted_program_count++;

    if (routes_ai) {
        kolibri_ai_add_formula(routes_ai, &slot->formula);
    }

    char buffer[256];
    snprintf(buffer,
             sizeof(buffer),
             "{\"program_id\":\"%s\",\"poe\":%.6f,\"mdl\":%.6f,\"score\":%.6f,\"accepted\":true}",
             slot->formula.id,
             slot->poe,
             slot->mdl,
             slot->score);
    return respond_json(resp, buffer, 200);
}

static int handle_chain_submit(const char *body, http_response_t *resp) {
    if (!body) {
        return respond_error(resp, 400, "bad_request", "missing body");
    }
    if (!routes_blockchain) {
        return respond_error(resp, 503, "unavailable", "blockchain is not attached");
    }
    char program_id[64];
    if (json_extract_string(body, "program_id", program_id, sizeof(program_id)) != 0) {
        return respond_error(resp, 400, "bad_request", "program_id required");
    }
    submitted_program_t *entry = find_submitted_program(program_id);
    if (!entry) {
        return respond_error(resp, 404, "not_found", "program is unknown");
    }
    Formula *formulas[1] = {&entry->formula};
    if (!blockchain_add_block(routes_blockchain, formulas, 1)) {
        return respond_error(resp, 500, "internal_error", "blockchain rejected block");
    }
    const char *hash = blockchain_get_last_hash(routes_blockchain);
    size_t height = routes_blockchain->block_count;
    char buffer[256];
    snprintf(buffer,
             sizeof(buffer),
             "{\"status\":\"accepted\",\"block_hash\":\"%s\",\"height\":%zu,\"program_id\":\"%s\"}",
             hash ? hash : "",
             height,
             entry->formula.id);
    return respond_json(resp, buffer, 200);
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
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/vm/run") == 0) {
        return handle_vm_run(cfg, body, resp);
    }
    size_t fkv_len = strlen("/api/v1/fkv/get");
    if (strcmp(method, "GET") == 0 && strncmp(path, "/api/v1/fkv/get", fkv_len) == 0 &&
        (path[fkv_len] == '\0' || path[fkv_len] == '?')) {
        return handle_fkv_get(path, resp);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/program/submit") == 0) {
        return handle_program_submit(cfg, body, resp);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/chain/submit") == 0) {
        return handle_chain_submit(body, resp);
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
    routes_blockchain = chain;
}

void http_routes_set_ai(KolibriAI *ai) {
    routes_ai = ai;
}
