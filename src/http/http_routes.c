/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */
#define _POSIX_C_SOURCE 200809L
#include "http/http_routes.h"

#include "blockchain.h"
#include "fkv/fkv.h"
#include "kolibri_ai.h"
#include "synthesis/formula_vm_eval.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
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
} stored_program_t;

static stored_program_t *stored_programs = NULL;
static size_t stored_program_count = 0;
static size_t stored_program_capacity = 0;
static uint64_t stored_program_seq = 0;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} json_buffer_t;

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

static int jb_reserve(json_buffer_t *jb, size_t extra) {
    if (!jb) {
        return -1;
    }
    size_t needed = jb->len + extra + 1;
    if (needed <= jb->cap) {
        return 0;
    }
    size_t new_cap = jb->cap ? jb->cap * 2 : 128;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    char *tmp = realloc(jb->data, new_cap);
    if (!tmp) {
        return -1;
    }
    jb->data = tmp;
    jb->cap = new_cap;
    return 0;
}

static int jb_append_format(json_buffer_t *jb, const char *fmt, ...) {
    if (!jb || !fmt) {
        return -1;
    }
    while (1) {
        if (jb_reserve(jb, 64) != 0) {
            return -1;
        }
        size_t avail = jb->cap - jb->len;
        va_list ap;
        va_start(ap, fmt);
        int written = vsnprintf(jb->data + jb->len, avail, fmt, ap);
        va_end(ap);
        if (written < 0) {
            return -1;
        }
        if ((size_t)written < avail) {
            jb->len += (size_t)written;
            return 0;
        }
        if (jb_reserve(jb, (size_t)written) != 0) {
            return -1;
        }
    }
}

static void jb_free(json_buffer_t *jb) {
    if (!jb) {
        return;
    }
    free(jb->data);
    jb->data = NULL;
    jb->len = 0;
    jb->cap = 0;
}

static int respond_with_buffer(http_response_t *resp, json_buffer_t *jb, int status) {
    if (!jb) {
        return -1;
    }
    if (jb_reserve(jb, 0) != 0) {
        jb_free(jb);
        return -1;
    }
    if (!jb->data) {
        if (jb_reserve(jb, 1) != 0) {
            jb_free(jb);
            return -1;
        }
        jb->data[0] = '\0';
    }
    int rc = respond_json(resp, jb->data, status);
    jb_free(jb);
    return rc;
}

static const char *skip_ws(const char *ptr, const char *end) {
    while (ptr < end && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    return ptr;
}

static const char *json_find_field(const char *body, size_t len, const char *key) {
    if (!body || !key) {
        return NULL;
    }
    char pattern[64];
    size_t key_len = strlen(key);
    if (key_len + 2 >= sizeof(pattern)) {
        return NULL;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *cursor = body;
    const char *end = body + len;
    size_t pattern_len = strlen(pattern);
    while (cursor && cursor < end) {
        cursor = strstr(cursor, pattern);
        if (!cursor || cursor >= end) {
            return NULL;
        }
        const char *p = cursor + pattern_len;
        p = skip_ws(p, end);
        if (p >= end || *p != ':') {
            cursor = cursor + 1;
            continue;
        }
        p++;
        p = skip_ws(p, end);
        if (p >= end) {
            return NULL;
        }
        return p;
    }
    return NULL;
}

static int json_get_string(const char *body,
                           size_t len,
                           const char *key,
                           char *out,
                           size_t out_size) {
    if (!out || out_size == 0) {
        return -1;
    }
    out[0] = '\0';
    const char *value = json_find_field(body, len, key);
    if (!value || *value != '"') {
        return -1;
    }
    value++;
    const char *end = body + len;
    size_t written = 0;
    while (value < end) {
        char ch = *value++;
        if (ch == '"') {
            if (written >= out_size) {
                return -1;
            }
            out[written] = '\0';
            return 0;
        }
        if (written + 1 >= out_size) {
            return -1;
        }
        out[written++] = ch;
    }
    return -1;
}

static int json_get_uint(const char *body, size_t len, const char *key, uint32_t *out) {
    if (!out) {
        return -1;
    }
    const char *value = json_find_field(body, len, key);
    if (!value) {
        return -1;
    }
    const char *end = body + len;
    char *next = NULL;
    unsigned long parsed = strtoul(value, &next, 10);
    if (value == next || next > end) {
        return -1;
    }
    *out = (uint32_t)parsed;
    return 0;
}

static int json_get_uint_array(const char *body,
                               size_t len,
                               const char *key,
                               uint8_t **out,
                               size_t *out_len) {
    if (!out || !out_len) {
        return -1;
    }
    const char *value = json_find_field(body, len, key);
    if (!value || *value != '[') {
        return -1;
    }
    const char *end = body + len;
    value++;
    uint8_t *buffer = NULL;
    size_t capacity = 0;
    size_t count = 0;
    while (value < end) {
        value = skip_ws(value, end);
        if (value >= end) {
            free(buffer);
            return -1;
        }
        if (*value == ']') {
            value++;
            break;
        }
        char *next = NULL;
        long parsed = strtol(value, &next, 10);
        if (value == next || next > end || parsed < 0 || parsed > 255) {
            free(buffer);
            return -1;
        }
        if (count >= capacity) {
            size_t new_cap = capacity ? capacity * 2 : 8;
            uint8_t *tmp = realloc(buffer, new_cap);
            if (!tmp) {
                free(buffer);
                return -1;
            }
            buffer = tmp;
            capacity = new_cap;
        }
        buffer[count++] = (uint8_t)parsed;
        value = skip_ws(next, end);
        if (value >= end) {
            free(buffer);
            return -1;
        }
        if (*value == ',') {
            value++;
            continue;
        }
        if (*value == ']') {
            value++;
            break;
        }
        free(buffer);
        return -1;
    }
    if (count == 0) {
        free(buffer);
        return -1;
    }
    *out = buffer;
    *out_len = count;
    return 0;
}

static const char *vm_status_to_string(vm_status_t status) {
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
        return "unknown";
    }
}

static int parse_decimal_digits(const char *text, uint8_t **out_digits, size_t *out_len) {
    if (!text || !out_digits || !out_len) {
        return -1;
    }
    size_t len = strlen(text);
    if (len == 0) {
        return -1;
    }
    uint8_t *digits = malloc(len);
    if (!digits) {
        return -1;
    }
    size_t count = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (!isdigit(ch)) {
            free(digits);
            return -1;
        }
        digits[count++] = (uint8_t)(ch - '0');
    }
    *out_digits = digits;
    *out_len = count;
    return 0;
}

static int ensure_program_capacity(size_t desired) {
    if (desired <= stored_program_capacity) {
        return 0;
    }
    size_t new_cap = stored_program_capacity ? stored_program_capacity * 2 : 8;
    while (new_cap < desired) {
        new_cap *= 2;
    }
    stored_program_t *tmp = realloc(stored_programs, new_cap * sizeof(stored_program_t));
    if (!tmp) {
        return -1;
    }
    stored_programs = tmp;
    stored_program_capacity = new_cap;
    return 0;
}

static stored_program_t *find_program_by_id(const char *program_id) {
    if (!program_id) {
        return NULL;
    }
    for (size_t i = 0; i < stored_program_count; ++i) {
        if (strcmp(stored_programs[i].formula.id, program_id) == 0) {
            return &stored_programs[i];
        }
    }
    return NULL;
}

static void format_bytecode_string(const uint8_t *code, size_t len, char *out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!code || len == 0) {
        return;
    }
    size_t written = 0;
    for (size_t i = 0; i < len; ++i) {
        int needed = snprintf(out + written, out_size - written, i + 1 < len ? "%u " : "%u", code[i]);
        if (needed < 0) {
            break;
        }
        written += (size_t)needed;
        if (written >= out_size) {
            out[out_size - 1] = '\0';
            break;
        }
    }
}

static int respond_error(http_response_t *resp, int status, const char *code, const char *message) {
    json_buffer_t jb = {0};
    if (jb_append_format(&jb,
                         "{\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}",
                         code ? code : "internal_error",
                         message ? message : "internal error") != 0) {
        jb_free(&jb);
        return -1;
    }
    return respond_with_buffer(resp, &jb, status);
}

static int parse_bytecode_from_json(const char *body,
                                    size_t body_len,
                                    char *program_repr,
                                    size_t repr_size,
                                    uint8_t **out_code,
                                    size_t *out_len) {
    if (!body || !out_code || !out_len) {
        return -1;
    }
    *out_code = NULL;
    *out_len = 0;
    if (program_repr && repr_size) {
        program_repr[0] = '\0';
    }

    char buffer[512];
    if (program_repr && repr_size && json_get_string(body, body_len, "program", buffer, sizeof(buffer)) == 0) {
        if (strlen(buffer) + 1 >= repr_size) {
            return -1;
        }
        strcpy(program_repr, buffer);
        if (formula_vm_compile_from_text(program_repr, out_code, out_len) != 0) {
            return -1;
        }
        return 0;
    }

    uint8_t *code = NULL;
    size_t code_len = 0;
    if (json_get_uint_array(body, body_len, "program", &code, &code_len) == 0) {
        *out_code = code;
        *out_len = code_len;
        return 0;
    }
    if (json_get_uint_array(body, body_len, "bytecode", &code, &code_len) == 0) {
        *out_code = code;
        *out_len = code_len;
        return 0;
    }
    return -1;
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

static int vm_trace_to_json(const vm_trace_t *trace, json_buffer_t *jb) {
    if (!jb) {
        return -1;
    }
    if (!trace || trace->count == 0) {
        return jb_append_format(jb, "[]");
    }
    if (jb_append_format(jb, "[") != 0) {
        return -1;
    }
    for (size_t i = 0; i < trace->count; ++i) {
        const vm_trace_entry_t *entry = &trace->entries[i];
        if (jb_append_format(jb,
                             "%s{\"step\":%u,\"ip\":%u,\"opcode\":%u,\"stack_top\":%llu,\"gas_left\":%u}",
                             i == 0 ? "" : ",",
                             entry->step,
                             entry->ip,
                             entry->opcode,
                             (unsigned long long)entry->stack_top,
                             entry->gas_left) != 0) {
            return -1;
        }
    }
    return jb_append_format(jb, "]");
}

static int handle_vm_run(const kolibri_config_t *cfg,
                         const char *body,
                         size_t body_len,
                         http_response_t *resp) {
    if (!body || body_len == 0) {
        return respond_error(resp, 400, "bad_request", "invalid JSON payload");
    }

    char program_repr[512];
    uint8_t *code = NULL;
    size_t code_len = 0;
    if (parse_bytecode_from_json(body, body_len, program_repr, sizeof(program_repr), &code, &code_len) != 0) {
        return respond_error(resp, 400, "bad_request", "missing or invalid program");
    }

    uint32_t gas_limit = cfg && cfg->vm.max_steps ? cfg->vm.max_steps : 256u;
    uint32_t tmp = 0;
    if (json_get_uint(body, body_len, "gas_limit", &tmp) == 0 && tmp > 0) {
        gas_limit = tmp;
    }
    uint32_t max_stack = cfg && cfg->vm.max_stack ? cfg->vm.max_stack : 64u;

    size_t trace_capacity = cfg && cfg->vm.trace_depth ? cfg->vm.trace_depth : 32u;
    vm_trace_entry_t *trace_entries = NULL;
    if (trace_capacity > 0) {
        trace_entries = calloc(trace_capacity, sizeof(vm_trace_entry_t));
    }
    vm_trace_t trace = {trace_entries, trace_capacity, 0, 0};

    prog_t prog = {code, code_len};
    vm_limits_t limits = {gas_limit, max_stack};
    vm_result_t result = {0};
    int rc = vm_run(&prog, &limits, trace_entries ? &trace : NULL, &result);
    free(code);
    if (rc != 0) {
        free(trace_entries);
        return respond_error(resp, 500, "internal_error", "vm execution failed");
    }

    json_buffer_t jb = {0};
    if (jb_append_format(&jb,
                         "{\"status\":\"%s\",\"halted\":%s,\"steps\":%u,\"gas_used\":%u",
                         vm_status_to_string(result.status),
                         result.halted ? "true" : "false",
                         result.steps,
                         result.steps) != 0) {
        free(trace_entries);
        jb_free(&jb);
        return -1;
    }

    if (result.status == VM_OK) {
        unsigned long long value = (unsigned long long)result.result;
        if (jb_append_format(&jb, ",\"result\":\"%llu\",\"stack\":[\"%llu\"]", value, value) != 0) {
            free(trace_entries);
            jb_free(&jb);
            return -1;
        }
    }

    if (program_repr[0] != '\0') {
        if (jb_append_format(&jb, ",\"program_source\":\"%s\"", program_repr) != 0) {
            free(trace_entries);
            jb_free(&jb);
            return -1;
        }
    }

    if (jb_append_format(&jb, ",\"trace\":") != 0 || vm_trace_to_json(&trace, &jb) != 0 || jb_append_format(&jb, "}") != 0) {
        free(trace_entries);
        jb_free(&jb);
        return -1;
    }

    free(trace_entries);
    return respond_with_buffer(resp, &jb, 200);
}

static int decode_query_params(const char *path, char **out_prefix, size_t *out_limit) {
    if (!path || !out_prefix || !out_limit) {
        return -1;
    }
    *out_prefix = NULL;
    *out_limit = 32;

    const char *query = strchr(path, '?');
    if (!query || !*(query + 1)) {
        return -1;
    }
    query++;

    char *query_copy = duplicate_string(query);
    if (!query_copy) {
        return -1;
    }

    char *saveptr = NULL;
    for (char *token = strtok_r(query_copy, "&", &saveptr); token; token = strtok_r(NULL, "&", &saveptr)) {
        char *eq = strchr(token, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char *key = token;
        const char *value = eq + 1;
        if (strcmp(key, "prefix") == 0) {
            free(*out_prefix);
            *out_prefix = duplicate_string(value);
        } else if (strcmp(key, "limit") == 0) {
            long l = strtol(value, NULL, 10);
            if (l > 0) {
                *out_limit = (size_t)l;
            }
        }
    }

    free(query_copy);
    return *out_prefix ? 0 : -1;
}

static int handle_fkv_get(const char *path, http_response_t *resp) {
    char *prefix_str = NULL;
    size_t limit = 0;
    if (decode_query_params(path, &prefix_str, &limit) != 0) {
        free(prefix_str);
        return respond_error(resp, 400, "bad_request", "prefix is required");
    }

    uint8_t *prefix_digits = NULL;
    size_t prefix_len = 0;
    if (parse_decimal_digits(prefix_str, &prefix_digits, &prefix_len) != 0) {
        free(prefix_str);
        return respond_error(resp, 400, "bad_request", "prefix must be decimal digits");
    }
    free(prefix_str);

    fkv_iter_t it = {0};
    if (fkv_get_prefix(prefix_digits, prefix_len, &it, limit) != 0) {
        free(prefix_digits);
        return respond_error(resp, 500, "internal_error", "fkv lookup failed");
    }
    free(prefix_digits);

    json_buffer_t jb = {0};
    if (jb_append_format(&jb, "{\"values\":[") != 0) {
        fkv_iter_free(&it);
        jb_free(&jb);
        return -1;
    }

    int first_value = 1;
    for (size_t i = 0; i < it.count; ++i) {
        fkv_entry_t *entry = &it.entries[i];
        if (entry->type != FKV_ENTRY_TYPE_VALUE) {
            continue;
        }
        char key_buf[256];
        size_t key_len = entry->key_len < sizeof(key_buf) - 1 ? entry->key_len : sizeof(key_buf) - 1;
        for (size_t j = 0; j < key_len; ++j) {
            key_buf[j] = (char)('0' + (entry->key[j] % 10));
        }
        key_buf[key_len] = '\0';

        char value_buf[512];
        size_t value_len = entry->value_len < sizeof(value_buf) - 1 ? entry->value_len : sizeof(value_buf) - 1;
        for (size_t j = 0; j < value_len; ++j) {
            value_buf[j] = (char)('0' + (entry->value[j] % 10));
        }
        value_buf[value_len] = '\0';

        if (jb_append_format(&jb,
                              "%s{\"key\":\"%s\",\"value\":\"%s\"}",
                              first_value ? "" : ",",
                              key_buf,
                              value_buf) != 0) {
            fkv_iter_free(&it);
            jb_free(&jb);
            return -1;
        }
        first_value = 0;
    }

    if (jb_append_format(&jb, "],\"programs\":[") != 0) {
        fkv_iter_free(&it);
        jb_free(&jb);
        return -1;
    }

    int first_program = 1;
    for (size_t i = 0; i < it.count; ++i) {
        fkv_entry_t *entry = &it.entries[i];
        if (entry->type != FKV_ENTRY_TYPE_PROGRAM) {
            continue;
        }
        char key_buf[256];
        size_t key_len = entry->key_len < sizeof(key_buf) - 1 ? entry->key_len : sizeof(key_buf) - 1;
        for (size_t j = 0; j < key_len; ++j) {
            key_buf[j] = (char)('0' + (entry->key[j] % 10));
        }
        key_buf[key_len] = '\0';

        char program_buf[512];
        size_t program_len = entry->value_len < sizeof(program_buf) - 1 ? entry->value_len : sizeof(program_buf) - 1;
        for (size_t j = 0; j < program_len; ++j) {
            program_buf[j] = (char)('0' + (entry->value[j] % 10));
        }
        program_buf[program_len] = '\0';

        if (jb_append_format(&jb,
                              "%s{\"key\":\"%s\",\"program\":\"%s\"}",
                              first_program ? "" : ",",
                              key_buf,
                              program_buf) != 0) {
            fkv_iter_free(&it);
            jb_free(&jb);
            return -1;
        }
        first_program = 0;
    }

    if (jb_append_format(&jb, "]}") != 0) {
        fkv_iter_free(&it);
        jb_free(&jb);
        return -1;
    }

    fkv_iter_free(&it);
    return respond_with_buffer(resp, &jb, 200);
}

static double compute_effectiveness(const vm_result_t *result, size_t bytecode_len, uint32_t gas_limit) {
    if (!result || result->status != VM_OK) {
        return 0.0;
    }
    double gas_ratio = gas_limit ? (double)(gas_limit - result->steps) / (double)gas_limit : 1.0;
    if (gas_ratio < 0.0) {
        gas_ratio = 0.0;
    }
    double length_penalty = bytecode_len ? 1.0 / (1.0 + (double)bytecode_len / 64.0) : 1.0;
    double value_bonus = log1p((double)result->result) / 8.0;
    if (value_bonus > 1.0) {
        value_bonus = 1.0;
    }
    double effectiveness = 0.5 * gas_ratio + 0.4 * length_penalty + 0.1 * value_bonus;
    if (effectiveness < 0.0) {
        effectiveness = 0.0;
    }
    if (effectiveness > 1.0) {
        effectiveness = 1.0;
    }
    return effectiveness;
}

static int handle_program_submit(const kolibri_config_t *cfg,
                                 const char *body,
                                 size_t body_len,
                                 http_response_t *resp) {
    if (!body || body_len == 0) {
        return respond_error(resp, 400, "bad_request", "invalid JSON payload");
    }

    char program_repr[512];
    uint8_t *code = NULL;
    size_t code_len = 0;
    if (parse_bytecode_from_json(body, body_len, program_repr, sizeof(program_repr), &code, &code_len) != 0) {
        return respond_error(resp, 400, "bad_request", "bytecode is required");
    }

    uint32_t gas_limit = cfg && cfg->vm.max_steps ? cfg->vm.max_steps : 256u;
    uint32_t max_stack = cfg && cfg->vm.max_stack ? cfg->vm.max_stack : 64u;

    prog_t prog = {code, code_len};
    vm_limits_t limits = {gas_limit, max_stack};
    vm_result_t result = {0};
    int rc = vm_run(&prog, &limits, NULL, &result);
    free(code);
    if (rc != 0) {
        return respond_error(resp, 500, "internal_error", "vm execution failed");
    }

    double effectiveness = compute_effectiveness(&result, code_len, gas_limit);

    stored_program_t program = {0};
    Formula *formula = &program.formula;
    memset(formula, 0, sizeof(*formula));
    snprintf(formula->id, sizeof(formula->id), "program-%llu", (unsigned long long)(++stored_program_seq));
    formula->effectiveness = effectiveness;
    formula->created_at = time(NULL);
    formula->tests_passed = (result.status == VM_OK) ? 1u : 0u;
    formula->confirmations = 0;
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    formula->type = FORMULA_LINEAR;
    if (program_repr[0] != '\0') {
        snprintf(formula->content, sizeof(formula->content), "%s", program_repr);
    }

    if (program_repr[0] == '\0') {
        char bytecode_repr[1024];
        format_bytecode_string(prog.code, prog.len, bytecode_repr, sizeof(bytecode_repr));
        snprintf(formula->content, sizeof(formula->content), "%s", bytecode_repr);
    }

    double poe = 0.0;
    double mdl = 0.0;
    double score = blockchain_score_formula(formula, &poe, &mdl);
    program.poe = poe;
    program.mdl = mdl;
    program.score = score;

    if (ensure_program_capacity(stored_program_count + 1) != 0) {
        return respond_error(resp, 500, "internal_error", "out of memory");
    }
    stored_programs[stored_program_count++] = program;

    if (routes_ai) {
        kolibri_ai_add_formula(routes_ai, formula);
    }

    json_buffer_t jb = {0};
    if (jb_append_format(&jb,
                         "{\"program_id\":\"%s\",\"poe\":%.6f,\"mdl\":%.6f,\"score\":%.6f,\"accepted\":%s,\"vm_status\":\"%s\"}",
                         formula->id,
                         poe,
                         mdl,
                         score,
                         result.status == VM_OK ? "true" : "false",
                         vm_status_to_string(result.status)) != 0) {
        jb_free(&jb);
        return -1;
    }
    return respond_with_buffer(resp, &jb, 200);
}

static int handle_chain_submit(const char *body, size_t body_len, http_response_t *resp) {
    if (!routes_blockchain) {
        return respond_error(resp, 503, "service_unavailable", "blockchain is not attached");
    }
    if (!body || body_len == 0) {
        return respond_error(resp, 400, "bad_request", "invalid JSON payload");
    }

    char program_id[128];
    if (json_get_string(body, body_len, "program_id", program_id, sizeof(program_id)) != 0) {
        return respond_error(resp, 400, "bad_request", "program_id is required");
    }

    stored_program_t *stored = find_program_by_id(program_id);
    if (!stored) {
        return respond_error(resp, 404, "not_found", "program not found");
    }

    Formula *formula = &stored->formula;
    Formula *formulas[1] = {formula};
    if (!blockchain_add_block(routes_blockchain, formulas, 1)) {
        return respond_error(resp, 500, "internal_error", "failed to append block");
    }

    const char *hash = blockchain_get_last_hash(routes_blockchain);
    json_buffer_t jb = {0};
    if (jb_append_format(&jb,
                         "{\"status\":\"accepted\",\"block_hash\":\"%s\",\"height\":%zu,\"poe\":%.6f,\"mdl\":%.6f,\"score\":%.6f}",
                         hash ? hash : "",
                         routes_blockchain->block_count,
                         stored->poe,
                         stored->mdl,
                         stored->score) != 0) {
        jb_free(&jb);
        return -1;
    }
    return respond_with_buffer(resp, &jb, 200);
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
        return handle_vm_run(cfg, body, body_len, resp);
    }
    if (strcmp(method, "GET") == 0 && strncmp(path, "/api/v1/fkv/get", strlen("/api/v1/fkv/get")) == 0) {
        return handle_fkv_get(path, resp);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/program/submit") == 0) {
        return handle_program_submit(cfg, body, body_len, resp);
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
    routes_blockchain = chain;
}

void http_routes_set_ai(KolibriAI *ai) {
    routes_ai = ai;
}
