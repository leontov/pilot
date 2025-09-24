/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */
#define _POSIX_C_SOURCE 200809L

#include "http/http_routes.h"

#include "blockchain.h"
#include "fkv/fkv.h"
#include "formula.h"
#include "kolibri_ai.h"
#include "util/log.h"

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    ROUTE_UNKNOWN = 0,
    ROUTE_DIALOG,
    ROUTE_HEALTH,
    ROUTE_METRICS,
    ROUTE_VM_RUN,
    ROUTE_FKV_GET,
    ROUTE_PROGRAM_SUBMIT,
    ROUTE_CHAIN_SUBMIT,
    ROUTE_AI_STATE,
    ROUTE_AI_FORMULAS,
    ROUTE_AI_SNAPSHOT_GET,
    ROUTE_AI_SNAPSHOT_POST,
    ROUTE_STUDIO_STATE,
} route_kind_t;

typedef struct {
    const char *route;
    uint64_t total;
    uint64_t errors;
    double duration_ms_sum;
    size_t bytes_in;
    size_t bytes_out;
} route_metrics_t;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} json_builder_t;

typedef struct {
    int active;
    char id[64];
    Formula formula;
} submitted_program_t;

static uint64_t routes_start_time = 0;
static Blockchain *routes_blockchain = NULL;
static KolibriAI *routes_ai = NULL;

static pthread_mutex_t metrics_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t program_lock = PTHREAD_MUTEX_INITIALIZER;

static route_metrics_t route_metrics[] = {
    [ROUTE_UNKNOWN] = {"unknown", 0, 0, 0.0, 0, 0},
    [ROUTE_DIALOG] = {"/api/v1/dialog", 0, 0, 0.0, 0, 0},
    [ROUTE_HEALTH] = {"/api/v1/health", 0, 0, 0.0, 0, 0},
    [ROUTE_METRICS] = {"/api/v1/metrics", 0, 0, 0.0, 0, 0},
    [ROUTE_VM_RUN] = {"/api/v1/vm/run", 0, 0, 0.0, 0, 0},
    [ROUTE_FKV_GET] = {"/api/v1/fkv/get", 0, 0, 0.0, 0, 0},
    [ROUTE_PROGRAM_SUBMIT] = {"/api/v1/program/submit", 0, 0, 0.0, 0, 0},
    [ROUTE_CHAIN_SUBMIT] = {"/api/v1/chain/submit", 0, 0, 0.0, 0, 0},
    [ROUTE_AI_STATE] = {"/api/v1/ai/state", 0, 0, 0.0, 0, 0},
    [ROUTE_AI_FORMULAS] = {"/api/v1/ai/formulas", 0, 0, 0.0, 0, 0},
    [ROUTE_AI_SNAPSHOT_GET] = {"/api/v1/ai/snapshot", 0, 0, 0.0, 0, 0},
    [ROUTE_AI_SNAPSHOT_POST] = {"/api/v1/ai/snapshot", 0, 0, 0.0, 0, 0},
    [ROUTE_STUDIO_STATE] = {"/api/v1/studio/state", 0, 0, 0.0, 0, 0},
};

static submitted_program_t *submitted_programs = NULL;
static size_t submitted_program_count = 0;
static size_t submitted_program_capacity = 0;

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

static int jb_init(json_builder_t *b, size_t initial) {
    b->data = malloc(initial);
    if (!b->data) {
        return -1;
    }
    b->len = 0;
    b->cap = initial;
    b->data[0] = '\0';
    return 0;
}

static int jb_reserve(json_builder_t *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) {
        return 0;
    }
    size_t new_cap = b->cap ? b->cap : 64;
    while (new_cap < b->len + extra + 1) {
        new_cap *= 2;
    }
    char *tmp = realloc(b->data, new_cap);
    if (!tmp) {
        return -1;
    }
    b->data = tmp;
    b->cap = new_cap;
    return 0;
}

static int jb_append(json_builder_t *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        return -1;
    }
    if (jb_reserve(b, (size_t)needed) != 0) {
        return -1;
    }
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += (size_t)needed;
    return 0;
}

static char *jb_steal(json_builder_t *b) {
    char *data = b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return data;
}

static void jb_free(json_builder_t *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static const char *find_key(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    return strstr(json, pattern);
}

static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static int json_get_string(const char *json, const char *key, char *out, size_t out_len) {
    if (!json || !key || !out || out_len == 0) {
        return -1;
    }
    const char *pos = find_key(json, key);
    if (!pos) {
        return -1;
    }
    pos = strchr(pos, ':');
    if (!pos) {
        return -1;
    }
    pos = skip_ws(pos + 1);
    if (*pos != '"') {
        return -1;
    }
    pos++;
    size_t i = 0;
    while (*pos && *pos != '"') {
        if (i + 1 >= out_len) {
            return -1;
        }
        out[i++] = *pos++;
    }
    if (*pos != '"') {
        return -1;
    }
    out[i] = '\0';
    return 0;
}

static int json_get_double(const char *json, const char *key, double *out) {
    if (!json || !key || !out) {
        return -1;
    }
    const char *pos = find_key(json, key);
    if (!pos) {
        return -1;
    }
    pos = strchr(pos, ':');
    if (!pos) {
        return -1;
    }
    pos = skip_ws(pos + 1);
    char *end = NULL;
    double value = strtod(pos, &end);
    if (end == pos) {
        return -1;
    }
    *out = value;
    return 0;
}

static int json_get_uint32(const char *json, const char *key, uint32_t *out) {
    if (!json || !key || !out) {
        return -1;
    }
    const char *pos = find_key(json, key);
    if (!pos) {
        return -1;
    }
    pos = strchr(pos, ':');
    if (!pos) {
        return -1;
    }
    pos = skip_ws(pos + 1);
    char *end = NULL;
    unsigned long value = strtoul(pos, &end, 10);
    if (end == pos) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static int json_get_bool(const char *json, const char *key, int *out) {
    if (!json || !key || !out) {
        return -1;
    }
    const char *pos = find_key(json, key);
    if (!pos) {
        return -1;
    }
    pos = strchr(pos, ':');
    if (!pos) {
        return -1;
    }
    pos = skip_ws(pos + 1);
    if (strncmp(pos, "true", 4) == 0) {
        *out = 1;
        return 0;
    }
    if (strncmp(pos, "false", 5) == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

static int json_get_int_array(const char *json,
                              const char *key,
                              int *out,
                              size_t max_items,
                              size_t *out_len) {
    if (!json || !key || !out || !out_len) {
        return -1;
    }
    const char *pos = find_key(json, key);
    if (!pos) {
        return -1;
    }
    pos = strchr(pos, ':');
    if (!pos) {
        return -1;
    }
    pos = strchr(pos, '[');
    if (!pos) {
        return -1;
    }
    pos++;
    size_t count = 0;
    while (*pos && *pos != ']') {
        pos = skip_ws(pos);
        if (*pos == ']') {
            break;
        }
        char *end = NULL;
        long value = strtol(pos, &end, 10);
        if (end == pos) {
            return -1;
        }
        if (count < max_items) {
            out[count++] = (int)value;
        }
        pos = end;
        pos = skip_ws(pos);
        if (*pos == ',') {
            pos++;
        }
    }
    if (*pos != ']' && *pos != '\0') {
        return -1;
    }
    *out_len = count;
    return 0;
}

static route_kind_t identify_route(const char *method, const char *path) {
    if (!method || !path) {
        return ROUTE_UNKNOWN;
    }
    if (strcmp(method, "GET") == 0) {
        if (strncmp(path, "/api/v1/fkv/get", sizeof("/api/v1/fkv/get") - 1) == 0) {
            return ROUTE_FKV_GET;
        }
        if (strcmp(path, "/api/v1/health") == 0) {
            return ROUTE_HEALTH;
        }
        if (strcmp(path, "/api/v1/metrics") == 0) {
            return ROUTE_METRICS;
        }
        if (strncmp(path, "/api/v1/ai/state", sizeof("/api/v1/ai/state") - 1) == 0) {
            return ROUTE_AI_STATE;
        }
        if (strncmp(path, "/api/v1/ai/formulas", sizeof("/api/v1/ai/formulas") - 1) == 0) {
            return ROUTE_AI_FORMULAS;
        }
        if (strncmp(path, "/api/v1/ai/snapshot", sizeof("/api/v1/ai/snapshot") - 1) == 0) {
            return ROUTE_AI_SNAPSHOT_GET;
        }
        if (strcmp(path, "/api/v1/studio/state") == 0) {
            return ROUTE_STUDIO_STATE;
        }
    }
    if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/v1/dialog") == 0) {
            return ROUTE_DIALOG;
        }
        if (strcmp(path, "/api/v1/vm/run") == 0) {
            return ROUTE_VM_RUN;
        }
        if (strcmp(path, "/api/v1/program/submit") == 0) {
            return ROUTE_PROGRAM_SUBMIT;
        }
        if (strcmp(path, "/api/v1/chain/submit") == 0) {
            return ROUTE_CHAIN_SUBMIT;
        }
        if (strcmp(path, "/api/v1/ai/snapshot") == 0) {
            return ROUTE_AI_SNAPSHOT_POST;
        }
    }
    return ROUTE_UNKNOWN;
}

static void metrics_record_request(route_kind_t kind,
                                   size_t bytes_in,
                                   size_t bytes_out,
                                   int status) {
    pthread_mutex_lock(&metrics_lock);
    route_metrics[kind].total++;
    route_metrics[kind].bytes_in += bytes_in;
    route_metrics[kind].bytes_out += bytes_out;
    if (status >= 400) {
        route_metrics[kind].errors++;
    }
    pthread_mutex_unlock(&metrics_lock);
}

static void metrics_record_duration(route_kind_t kind, double duration_ms) {
    pthread_mutex_lock(&metrics_lock);
    route_metrics[kind].duration_ms_sum += duration_ms;
    pthread_mutex_unlock(&metrics_lock);
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

static int respond_text(http_response_t *resp, const char *text, const char *content_type, int status) {
    if (!resp || !text || !content_type) {
        return -1;
    }
    char *data = duplicate_string(text);
    if (!data) {
        return -1;
    }
    resp->data = data;
    resp->len = strlen(data);
    resp->status = status;
    snprintf(resp->content_type, sizeof(resp->content_type), "%s", content_type);
    return 0;
}

static int respond_error(http_response_t *resp, int status, const char *code, const char *message) {
    if (!resp) {
        return -1;
    }
    json_builder_t builder;
    if (jb_init(&builder, 128) != 0) {
        return -1;
    }
    jb_append(&builder,
              "{\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}",
              code ? code : "internal_error",
              message ? message : "");
    char *json = jb_steal(&builder);
    if (!json) {
        jb_free(&builder);
        return -1;
    }
    int rc = respond_json(resp, json, status);
    free(json);
    return rc;
}

static submitted_program_t *registry_find(const char *id) {
    if (!id) {
        return NULL;
    }
    for (size_t i = 0; i < submitted_program_count; ++i) {
        if (submitted_programs[i].active && strcmp(submitted_programs[i].id, id) == 0) {
            return &submitted_programs[i];
        }
    }
    return NULL;
}

static int registry_store(const Formula *formula) {
    if (!formula || formula->id[0] == '\0') {
        return -1;
    }
    pthread_mutex_lock(&program_lock);
    submitted_program_t *slot = registry_find(formula->id);
    if (!slot) {
        if (submitted_program_count >= submitted_program_capacity) {
            size_t new_cap = submitted_program_capacity ? submitted_program_capacity * 2 : 8;
            submitted_program_t *tmp = realloc(submitted_programs, new_cap * sizeof(*tmp));
            if (!tmp) {
                pthread_mutex_unlock(&program_lock);
                return -1;
            }
            for (size_t i = submitted_program_capacity; i < new_cap; ++i) {
                memset(&tmp[i], 0, sizeof(tmp[i]));
            }
            submitted_programs = tmp;
            submitted_program_capacity = new_cap;
        }
        slot = &submitted_programs[submitted_program_count++];
    } else {
        formula_clear(&slot->formula);
    }
    slot->active = 1;
    memcpy(slot->id, formula->id, sizeof(slot->id));
    if (formula_copy(&slot->formula, formula) != 0) {
        slot->active = 0;
        memset(&slot->formula, 0, sizeof(slot->formula));
        pthread_mutex_unlock(&program_lock);
        return -1;
    }
    pthread_mutex_unlock(&program_lock);
    return 0;
}

static int handle_health(http_response_t *resp) {
    uint64_t uptime_ms = routes_start_time ? (now_ms() - routes_start_time) : 0;
    uint64_t total_requests = 0;
    uint64_t total_errors = 0;
    pthread_mutex_lock(&metrics_lock);
    for (size_t i = 0; i < sizeof(route_metrics) / sizeof(route_metrics[0]); ++i) {
        total_requests += route_metrics[i].total;
        total_errors += route_metrics[i].errors;
    }
    pthread_mutex_unlock(&metrics_lock);

    char buffer[256];
    snprintf(buffer,
             sizeof(buffer),
             "{\"status\":\"ok\",\"uptime_ms\":%llu,\"requests\":%llu,\"errors\":%llu,"
             "\"blockchain_attached\":%s,\"ai_attached\":%s}",
             (unsigned long long)uptime_ms,
             (unsigned long long)total_requests,
             (unsigned long long)total_errors,
             routes_blockchain ? "true" : "false",
             routes_ai ? "true" : "false");
    return respond_json(resp, buffer, 200);
}

static int handle_metrics(http_response_t *resp) {
    pthread_mutex_lock(&metrics_lock);
    size_t count = sizeof(route_metrics) / sizeof(route_metrics[0]);
    uint64_t total_requests = 0;
    for (size_t i = 0; i < count; ++i) {
        total_requests += route_metrics[i].total;
    }
    size_t capacity = 512 + count * 256;
    char *buffer = calloc(1, capacity);
    if (!buffer) {
        pthread_mutex_unlock(&metrics_lock);
        return -1;
    }
    size_t len = 0;
    len += snprintf(buffer + len,
                    capacity - len,
                    "# HELP kolibri_http_requests_total Total HTTP requests handled by Kolibri\n"
                    "# TYPE kolibri_http_requests_total counter\n"
                    "kolibri_http_requests_total %llu\n",
                    (unsigned long long)total_requests);
    len += snprintf(buffer + len,
                    capacity - len,
                    "# HELP kolibri_http_route_requests_total Requests by route\n"
                    "# TYPE kolibri_http_route_requests_total counter\n");
    for (size_t i = 0; i < count; ++i) {
        if (!route_metrics[i].route) {
            continue;
        }
        len += snprintf(buffer + len,
                        capacity - len,
                        "kolibri_http_route_requests_total{route=\"%s\"} %llu\n",
                        route_metrics[i].route,
                        (unsigned long long)route_metrics[i].total);
    }
    len += snprintf(buffer + len,
                    capacity - len,
                    "# HELP kolibri_http_route_errors_total Errors by route\n"
                    "# TYPE kolibri_http_route_errors_total counter\n");
    for (size_t i = 0; i < count; ++i) {
        if (!route_metrics[i].route) {
            continue;
        }
        len += snprintf(buffer + len,
                        capacity - len,
                        "kolibri_http_route_errors_total{route=\"%s\"} %llu\n",
                        route_metrics[i].route,
                        (unsigned long long)route_metrics[i].errors);
    }
    len += snprintf(buffer + len,
                    capacity - len,
                    "# HELP kolibri_http_request_duration_ms_sum Cumulative request duration in milliseconds\n"
                    "# TYPE kolibri_http_request_duration_ms_sum gauge\n");
    for (size_t i = 0; i < count; ++i) {
        if (!route_metrics[i].route) {
            continue;
        }
        len += snprintf(buffer + len,
                        capacity - len,
                        "kolibri_http_request_duration_ms_sum{route=\"%s\"} %.6f\n",
                        route_metrics[i].route,
                        route_metrics[i].duration_ms_sum);
    }
    len += snprintf(buffer + len,
                    capacity - len,
                    "# HELP kolibri_blockchain_blocks Number of blocks in the knowledge chain\n"
                    "# TYPE kolibri_blockchain_blocks gauge\n"
                    "kolibri_blockchain_blocks %zu\n",
                    routes_blockchain ? routes_blockchain->block_count : 0u);
    pthread_mutex_unlock(&metrics_lock);

    int rc = respond_text(resp, buffer, "text/plain; version=0.0.4", 200);
    free(buffer);
    return rc;
}

static int handle_dialog(const char *body, size_t body_len, http_response_t *resp) {
    (void)body_len;
    const char *reply = "{\"answer\":\"Kolibri is online\"}";
    if (body && strstr(body, "ping")) {
        reply = "{\"answer\":\"pong\"}";
    }
    log_info("{\"event\":\"dialog\",\"payload_size\":%zu}", body_len);
    return respond_json(resp, reply, 200);
}

static const char *vm_status_string(vm_status_t status) {
    switch (status) {
    case VM_OK:
        return "ok";
    case VM_ERR_INVALID_OPCODE:
        return "invalid_opcode";
    case VM_ERR_STACK_OVERFLOW:
        return "stack_overflow";
    case VM_ERR_STACK_UNDERFLOW:
        return "stack_underflow";
    case VM_ERR_DIV_BY_ZERO:
        return "div_by_zero";
    case VM_ERR_GAS_EXHAUSTED:
        return "gas_exhausted";
    default:
        return "error";
    }
}

static int handle_vm_run(const kolibri_config_t *cfg,
                         const char *body,
                         size_t body_len,
                         http_response_t *resp) {
    (void)body_len;
    if (!body) {
        return respond_error(resp, 400, "bad_request", "JSON body required");
    }
    int values[512];
    size_t value_count = 0;
    if (json_get_int_array(body, "bytecode", values, 512, &value_count) != 0 || value_count == 0) {
        return respond_error(resp, 400, "bad_request", "bytecode array is required");
    }
    uint8_t *code = malloc(value_count);
    if (!code) {
        return respond_error(resp, 500, "internal_error", "Failed to allocate bytecode buffer");
    }
    for (size_t i = 0; i < value_count; ++i) {
        if (values[i] < 0 || values[i] > 255) {
            free(code);
            return respond_error(resp, 400, "bad_request", "bytecode values must be between 0 and 255");
        }
        code[i] = (uint8_t)values[i];
    }

    vm_limits_t limits = {.max_steps = 1024, .max_stack = 128};
    if (cfg) {
        if (cfg->vm.max_steps) {
            limits.max_steps = cfg->vm.max_steps;
        }
        if (cfg->vm.max_stack) {
            limits.max_stack = cfg->vm.max_stack;
        }
    }
    uint32_t override = 0;
    if (json_get_uint32(body, "max_steps", &override) == 0 && override > 0) {
        limits.max_steps = override;
    }
    if (json_get_uint32(body, "max_stack", &override) == 0 && override > 0) {
        limits.max_stack = override;
    }
    int include_trace = 0;
    json_get_bool(body, "trace", &include_trace);

    size_t trace_capacity = (size_t)(cfg && cfg->vm.trace_depth ? cfg->vm.trace_depth : 32);
    vm_trace_entry_t *entries = NULL;
    vm_trace_t trace = {0};
    if (include_trace && trace_capacity > 0) {
        entries = calloc(trace_capacity, sizeof(vm_trace_entry_t));
        if (entries) {
            trace.entries = entries;
            trace.capacity = trace_capacity;
        }
    }

    prog_t prog = {.code = code, .len = value_count};
    vm_result_t result = {0};
    int rc = vm_run(&prog, &limits, entries ? &trace : NULL, &result);
    free(code);
    if (rc != 0) {
        free(entries);
        return respond_error(resp, 500, "vm_error", "vm_run failed");
    }

    json_builder_t builder;
    if (jb_init(&builder, 256) != 0) {
        free(entries);
        return respond_error(resp, 500, "internal_error", "Failed to allocate response");
    }
    jb_append(&builder,
              "{\"status\":\"%s\",\"result\":\"%llu\",\"steps\":%u,\"halted\":%s",
              vm_status_string(result.status),
              (unsigned long long)result.result,
              result.steps,
              result.halted ? "true" : "false");
    if (entries && trace.count > 0) {
        jb_append(&builder, ",\"trace\":[");
        for (size_t i = 0; i < trace.count; ++i) {
            if (i > 0) {
                jb_append(&builder, ",");
            }
            vm_trace_entry_t *e = &entries[i];
            jb_append(&builder,
                      "{\"step\":%u,\"ip\":%u,\"opcode\":%u,\"stack_top\":%lld,\"gas_left\":%u}",
                      e->step,
                      e->ip,
                      e->opcode,
                      (long long)e->stack_top,
                      e->gas_left);
        }
        jb_append(&builder, "]");
    }
    jb_append(&builder, "}");
    free(entries);

    char *json = jb_steal(&builder);
    if (!json) {
        jb_free(&builder);
        return respond_error(resp, 500, "internal_error", "Failed to allocate response");
    }
    int result_rc = respond_json(resp, json, 200);
    free(json);
    log_info("{\"event\":\"vm_run\",\"steps\":%u,\"status\":\"%s\"}",
             result.steps,
             vm_status_string(result.status));
    return result_rc;
}

static int parse_query_param(const char *query, const char *name, char *out, size_t out_len) {
    if (!query || !name || !out || out_len == 0) {
        return -1;
    }
    size_t name_len = strlen(name);
    const char *pos = query;
    while (pos && *pos) {
        if (strncmp(pos, name, name_len) == 0 && pos[name_len] == '=') {
            pos += name_len + 1;
            size_t i = 0;
            while (pos[i] && pos[i] != '&' && i + 1 < out_len) {
                out[i] = pos[i];
                i++;
            }
            out[i] = '\0';
            return 0;
        }
        const char *amp = strchr(pos, '&');
        if (!amp) {
            break;
        }
        pos = amp + 1;
    }
    return -1;
}

static int handle_fkv_get(const char *path, http_response_t *resp) {
    const char *query = strchr(path, '?');
    if (!query || !*(query + 1)) {
        return respond_error(resp, 400, "bad_request", "prefix query parameter is required");
    }
    query++;
    char prefix_str[256];
    if (parse_query_param(query, "prefix", prefix_str, sizeof(prefix_str)) != 0) {
        return respond_error(resp, 400, "bad_request", "prefix query parameter is required");
    }
    char limit_str[32];
    size_t limit = 16;
    if (parse_query_param(query, "limit", limit_str, sizeof(limit_str)) == 0) {
        size_t parsed = (size_t)strtoul(limit_str, NULL, 10);
        if (parsed > 0) {
            limit = parsed;
        }
    }
    if (limit > 128) {
        limit = 128;
    }

    uint8_t digits[256];
    size_t digits_len = 0;
    for (size_t i = 0; prefix_str[i]; ++i) {
        if (!isdigit((unsigned char)prefix_str[i])) {
            return respond_error(resp, 400, "bad_request", "prefix must be decimal");
        }
        digits[digits_len++] = (uint8_t)(prefix_str[i] - '0');
        if (digits_len >= sizeof(digits)) {
            break;
        }
    }

    fkv_iter_t it = {0};
    if (fkv_get_prefix(digits, digits_len, &it, limit) != 0) {
        return respond_error(resp, 500, "internal_error", "fkv_get_prefix failed");
    }

    json_builder_t builder;
    if (jb_init(&builder, 256) != 0) {
        fkv_iter_free(&it);
        return respond_error(resp, 500, "internal_error", "Failed to allocate response");
    }
    jb_append(&builder, "{\"prefix\":\"%s\",\"values\":[", prefix_str);
    size_t values_written = 0;
    size_t programs_written = 0;
    json_builder_t programs;
    jb_init(&programs, 128);
    jb_append(&programs, "[");

    for (size_t i = 0; i < it.count; ++i) {
        fkv_entry_t *entry = &it.entries[i];
        char key_buf[256];
        char val_buf[256];
        size_t key_len = entry->key_len < sizeof(key_buf) - 1 ? entry->key_len : sizeof(key_buf) - 1;
        size_t val_len = entry->value_len < sizeof(val_buf) - 1 ? entry->value_len : sizeof(val_buf) - 1;
        for (size_t k = 0; k < key_len; ++k) {
            key_buf[k] = (char)('0' + entry->key[k]);
        }
        key_buf[key_len] = '\0';
        for (size_t v = 0; v < val_len; ++v) {
            val_buf[v] = (char)('0' + entry->value[v]);
        }
        val_buf[val_len] = '\0';

        if (entry->type == FKV_ENTRY_TYPE_VALUE) {
            if (values_written > 0) {
                jb_append(&builder, ",");
            }
            jb_append(&builder, "{\"key\":\"%s\",\"value\":\"%s\"}", key_buf, val_buf);
            values_written++;
        } else {
            if (programs_written > 0) {
                jb_append(&programs, ",");
            }
            jb_append(&programs, "{\"key\":\"%s\",\"bytecode\":\"%s\"}", key_buf, val_buf);
            programs_written++;
        }
    }
    jb_append(&builder, "]");
    jb_append(&programs, "]");
    fkv_iter_free(&it);

    jb_append(&builder, ",\"programs\":%s}", programs.data ? programs.data : "[]");
    char *json = jb_steal(&builder);
    jb_free(&builder);
    jb_free(&programs);
    if (!json) {
        return respond_error(resp, 500, "internal_error", "Failed to allocate response");
    }
    int rc = respond_json(resp, json, 200);
    free(json);
    log_info("{\"event\":\"fkv_get\",\"prefix\":\"%s\",\"results\":%zu}", prefix_str, values_written + programs_written);
    return rc;
}

static int handle_program_submit(const char *body,
                                 size_t body_len,
                                 http_response_t *resp) {
    (void)body_len;
    if (!body) {
        return respond_error(resp, 400, "bad_request", "JSON body required");
    }
    Formula formula = {0};
    if (json_get_string(body, "program_id", formula.id, sizeof(formula.id)) != 0) {
        return respond_error(resp, 400, "bad_request", "program_id is required");
    }
    if (json_get_double(body, "effectiveness", &formula.effectiveness) != 0) {
        formula.effectiveness = 0.0;
    }
    char repr[16];
    if (json_get_string(body, "representation", repr, sizeof(repr)) == 0 && strcmp(repr, "analytic") == 0) {
        formula.representation = FORMULA_REPRESENTATION_ANALYTIC;
    } else {
        formula.representation = FORMULA_REPRESENTATION_TEXT;
    }
    if (formula.representation == FORMULA_REPRESENTATION_TEXT) {
        json_get_string(body, "content", formula.content, sizeof(formula.content));
    }
    formula.created_at = time(NULL);
    formula.type = FORMULA_LINEAR;

    double poe = 0.0;
    double mdl = 0.0;
    double score = blockchain_score_formula(&formula, &poe, &mdl);
    int accepted = score >= 0.5;

    if (routes_ai) {
        kolibri_ai_add_formula(routes_ai, &formula);
    }
    if (registry_store(&formula) != 0) {
        log_warn("Failed to store submitted formula %s", formula.id);
    }

    char buffer[256];
    snprintf(buffer,
             sizeof(buffer),
             "{\"program_id\":\"%s\",\"poe\":%.6f,\"mdl\":%.6f,\"score\":%.6f,\"accepted\":%s}",
             formula.id,
             poe,
             mdl,
             score,
             accepted ? "true" : "false");
    log_info("{\"event\":\"program_submit\",\"id\":\"%s\",\"score\":%.4f}", formula.id, score);
    return respond_json(resp, buffer, 200);
}

static int handle_chain_submit(const char *body,
                               size_t body_len,
                               http_response_t *resp) {
    (void)body_len;
    if (!routes_blockchain) {
        return respond_error(resp, 503, "blockchain_unavailable", "Blockchain module is not attached");
    }
    if (!body) {
        return respond_error(resp, 400, "bad_request", "JSON body required");
    }
    char program_id[64];
    if (json_get_string(body, "program_id", program_id, sizeof(program_id)) != 0) {
        return respond_error(resp, 400, "bad_request", "program_id is required");
    }
    pthread_mutex_lock(&program_lock);
    submitted_program_t *entry = registry_find(program_id);
    pthread_mutex_unlock(&program_lock);
    if (!entry) {
        return respond_error(resp, 404, "not_found", "Program not found. Submit first");
    }
    Formula *ptrs[1] = {&entry->formula};
    bool ok = blockchain_add_block(routes_blockchain, ptrs, 1);
    if (!ok) {
        return respond_error(resp, 500, "blockchain_error", "Failed to append block");
    }
    const char *hash = blockchain_get_last_hash(routes_blockchain);
    json_builder_t builder;
    if (jb_init(&builder, 128) != 0) {
        return respond_error(resp, 500, "internal_error", "Failed to allocate response");
    }
    jb_append(&builder,
              "{\"status\":\"accepted\",\"height\":%zu",
              routes_blockchain->block_count);
    if (hash) {
        jb_append(&builder, ",\"block_hash\":\"%s\"", hash);
    }
    jb_append(&builder, "}");
    char *json = jb_steal(&builder);
    if (!json) {
        jb_free(&builder);
        return respond_error(resp, 500, "internal_error", "Failed to allocate response");
    }
    int rc = respond_json(resp, json, 200);
    free(json);
    log_info("{\"event\":\"chain_submit\",\"id\":\"%s\",\"height\":%zu}", program_id, routes_blockchain->block_count);
    return rc;
}

static int handle_ai_state(http_response_t *resp) {
    if (!routes_ai) {
        return respond_error(resp, 503, "ai_unavailable", "Kolibri AI module is not attached");
    }
    char *payload = kolibri_ai_serialize_state(routes_ai);
    if (!payload) {
        return respond_error(resp, 500, "ai_error", "Failed to serialize AI state");
    }
    int rc = respond_json(resp, payload, 200);
    free(payload);
    return rc;
}

static int handle_ai_formulas(const char *path, http_response_t *resp) {
    if (!routes_ai) {
        return respond_error(resp, 503, "ai_unavailable", "Kolibri AI module is not attached");
    }
    size_t limit = 0;
    const char *query = strchr(path, '?');
    if (query && *(query + 1)) {
        char limit_str[32];
        if (parse_query_param(query + 1, "limit", limit_str, sizeof(limit_str)) == 0) {
            size_t parsed = (size_t)strtoul(limit_str, NULL, 10);
            if (parsed > 0) {
                limit = parsed;
            }
        }
    }
    char *payload = kolibri_ai_serialize_formulas(routes_ai, limit);
    if (!payload) {
        return respond_error(resp, 500, "ai_error", "Failed to serialize AI formulas");
    }
    int rc = respond_json(resp, payload, 200);
    free(payload);
    return rc;
}

static int handle_ai_snapshot_get(http_response_t *resp) {
    if (!routes_ai) {
        return respond_error(resp, 503, "ai_unavailable", "Kolibri AI module is not attached");
    }
    char *payload = kolibri_ai_export_snapshot(routes_ai);
    if (!payload) {
        return respond_error(resp, 500, "ai_error", "Failed to export snapshot");
    }
    int rc = respond_json(resp, payload, 200);
    free(payload);
    return rc;
}

static int handle_ai_snapshot_post(const char *body,
                                   size_t body_len,
                                   http_response_t *resp) {
    if (!routes_ai) {
        return respond_error(resp, 503, "ai_unavailable", "Kolibri AI module is not attached");
    }
    if (!body || body_len == 0) {
        return respond_error(resp, 400, "bad_request", "Snapshot payload required");
    }
    char *buffer = malloc(body_len + 1);
    if (!buffer) {
        return respond_error(resp, 500, "internal_error", "Failed to allocate snapshot buffer");
    }
    memcpy(buffer, body, body_len);
    buffer[body_len] = '\0';
    int rc = kolibri_ai_import_snapshot(routes_ai, buffer);
    free(buffer);
    if (rc != 0) {
        return respond_error(resp, 400, "bad_request", "Failed to import snapshot");
    }
    log_info("{\"event\":\"ai_snapshot_import\",\"bytes\":%zu}", body_len);
    return respond_json(resp, "{\"status\":\"ok\"}", 200);
}

static int handle_studio_state(http_response_t *resp) {
    uint64_t uptime_ms = routes_start_time ? (now_ms() - routes_start_time) : 0;
    json_builder_t builder;
    if (jb_init(&builder, 512) != 0) {
        return respond_error(resp, 500, "internal_error", "Failed to allocate response");
    }
    jb_append(&builder, "{\"uptime_ms\":%llu,\"http\":{\"routes\":[",
              (unsigned long long)uptime_ms);
    pthread_mutex_lock(&metrics_lock);
    int first = 1;
    for (size_t i = 0; i < sizeof(route_metrics) / sizeof(route_metrics[0]); ++i) {
        if (!route_metrics[i].route) {
            continue;
        }
        if (!first) {
            jb_append(&builder, ",");
        }
        first = 0;
        double avg = route_metrics[i].total ?
                         route_metrics[i].duration_ms_sum / (double)route_metrics[i].total :
                         0.0;
        jb_append(&builder,
                  "{\"route\":\"%s\",\"count\":%llu,\"errors\":%llu,\"avg_duration_ms\":%.6f}",
                  route_metrics[i].route,
                  (unsigned long long)route_metrics[i].total,
                  (unsigned long long)route_metrics[i].errors,
                  avg);
    }
    pthread_mutex_unlock(&metrics_lock);
    jb_append(&builder,
              "]},\"blockchain\":%zu,\"ai_attached\":%s}",
              routes_blockchain ? routes_blockchain->block_count : 0u,
              routes_ai ? "true" : "false");
    char *json = jb_steal(&builder);
    if (!json) {
        jb_free(&builder);
        return respond_error(resp, 500, "internal_error", "Failed to allocate response");
    }
    int rc = respond_json(resp, json, 200);
    free(json);
    return rc;
}

int http_handle_request(const kolibri_config_t *cfg,
                        const char *method,
                        const char *path,
                        const char *body,
                        size_t body_len,
                        http_response_t *resp) {
    if (!method || !path || !resp) {
        return -1;
    }

    route_kind_t kind = identify_route(method, path);
    int rc = -1;
    if (kind == ROUTE_HEALTH && strcmp(method, "GET") == 0) {
        rc = handle_health(resp);
    } else if (kind == ROUTE_METRICS && strcmp(method, "GET") == 0) {
        rc = handle_metrics(resp);
    } else if (kind == ROUTE_DIALOG && strcmp(method, "POST") == 0) {
        rc = handle_dialog(body, body_len, resp);
    } else if (kind == ROUTE_VM_RUN && strcmp(method, "POST") == 0) {
        rc = handle_vm_run(cfg, body, body_len, resp);
    } else if (kind == ROUTE_FKV_GET && strcmp(method, "GET") == 0) {
        rc = handle_fkv_get(path, resp);
    } else if (kind == ROUTE_PROGRAM_SUBMIT && strcmp(method, "POST") == 0) {
        rc = handle_program_submit(body, body_len, resp);
    } else if (kind == ROUTE_CHAIN_SUBMIT && strcmp(method, "POST") == 0) {
        rc = handle_chain_submit(body, body_len, resp);
    } else if (kind == ROUTE_AI_STATE && strcmp(method, "GET") == 0) {
        rc = handle_ai_state(resp);
    } else if (kind == ROUTE_AI_FORMULAS && strcmp(method, "GET") == 0) {
        rc = handle_ai_formulas(path, resp);
    } else if (kind == ROUTE_AI_SNAPSHOT_GET && strcmp(method, "GET") == 0) {
        rc = handle_ai_snapshot_get(resp);
    } else if (kind == ROUTE_AI_SNAPSHOT_POST && strcmp(method, "POST") == 0) {
        rc = handle_ai_snapshot_post(body, body_len, resp);
    } else if (kind == ROUTE_STUDIO_STATE && strcmp(method, "GET") == 0) {
        rc = handle_studio_state(resp);
    }

    if (kind == ROUTE_UNKNOWN || rc != 0) {
        if (kind == ROUTE_UNKNOWN) {
            respond_error(resp, 404, "not_found", "Route not found");
            rc = 0;
        } else if (rc != 0) {
            log_warn("Unhandled HTTP route %s %s", method, path);
        }
    }

    int status = resp->status ? resp->status : 200;
    metrics_record_request(kind, body_len, resp->len, status);
    return rc;
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

void http_metrics_observe(const char *method,
                          const char *path,
                          int status,
                          double duration_ms,
                          size_t bytes_in,
                          size_t bytes_out) {
    (void)status;
    (void)bytes_in;
    (void)bytes_out;
    route_kind_t kind = identify_route(method, path);
    metrics_record_duration(kind, duration_ms);
}

