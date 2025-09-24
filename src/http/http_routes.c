
/* Copyright (c) 2025 Кочуров Владислав Евгеньевич */
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

#define _POSIX_C_SOURCE 200809
#include "http/http_routes.h
#include <ctype.h>
#include "blockchain.h"
#include "http/http_routes.h"
#include "fkv/fkv.h"
#include "formula.h"
#include "vm/vm.h"
#include <json-c/json.h>
#include <ctype.h>
#include <math.h>
#include "kolibri_ai.h"
#include "synthesis/formula_vm_eval.h"
#include "vm/vm.h"
#include <ctype.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


typedef struct {
    char id[64];
    double poe;
    double mdl;
    double score;
    int accepted;
    uint8_t *bytecode;
    size_t bytecode_len;
    time_t submitted_at;
} program_record_t;

typedef struct {
    uint64_t total_requests;
    uint64_t total_errors;
    uint64_t vm_runs;
    uint64_t fkv_queries;
    uint64_t program_submissions;
    uint64_t chain_submissions;
    time_t last_block_time;
} route_metrics_t;

static uint64_t routes_start_time = 0;
static Blockchain *routes_blockchain = NULL;
static route_metrics_t routes_metrics = {0};
static program_record_t program_records[64];
static size_t program_record_count = 0;
static uint64_t program_id_counter = 0;

#include "formula_core.h"

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
static pthread_mutex_t dialog_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t dialog_exchange_counter = 0;


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

static void program_record_reset(program_record_t *rec) {
    if (!rec) {
        return;
    }
    free(rec->bytecode);
    memset(rec, 0, sizeof(*rec));
}

static program_record_t *program_record_find(const char *id) {
    if (!id) {
        return NULL;
    }
    for (size_t i = 0; i < program_record_count; ++i) {
        if (strcmp(program_records[i].id, id) == 0) {
            return &program_records[i];
        }
    }
    return NULL;
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
    if (status >= 400) {
        routes_metrics.total_errors++;
    }
    return 0;
}


static int respond_with_json_object(http_response_t *resp,
                                    struct json_object *obj,
                                    int status) {
    if (!resp || !obj) {
        return -1;
    }
    const char *payload = json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN);
    int rc = respond_json(resp, payload, status);
    json_object_put(obj);
    return rc;
}

static struct json_object *make_error_json(const char *message) {
    struct json_object *obj = json_object_new_object();
    if (!obj) {
        return NULL;
    }
    json_object_object_add(obj, "error", json_object_new_string(message ? message : "internal_error"));
    return obj;


static void free_submitted_programs(void) {
    free(submitted_programs);
    submitted_programs = NULL;
    submitted_program_count = 0;
    submitted_program_capacity = 0;
    submitted_program_counter = 0;
}

static const char *memmem_const(const char *haystack, size_t haystack_len, const char *needle, size_t needle_len) {
    if (!haystack || !needle || needle_len == 0 || haystack_len < needle_len) {
        return NULL;
    }
    for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
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

static void append_char(char **buffer, size_t *len, size_t *cap, char ch) {
    if (!buffer || !len || !cap) {
        return;
    }
    if (*len + 1 >= *cap) {
        size_t new_cap = (*cap == 0) ? 128 : (*cap * 2);
        char *tmp = realloc(*buffer, new_cap);
        if (!tmp) {
            return;
        }
        *buffer = tmp;
        *cap = new_cap;
    }
    (*buffer)[(*len)++] = ch;
    (*buffer)[*len] = '\0';
}

static int append_format(char **buffer, size_t *len, size_t *cap, const char *fmt, ...) {
    if (!buffer || !len || !cap || !fmt) {
        return -1;
    }
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) {
        return -1;
    }
    if (*len + (size_t)needed + 1 >= *cap) {
        size_t new_cap = (*cap == 0) ? (size_t)needed + 64 : *cap;
        while (*len + (size_t)needed + 1 >= new_cap) {
            new_cap *= 2;
        }
        char *tmp = realloc(*buffer, new_cap);
        if (!tmp) {
            return -1;
        }
        *buffer = tmp;
        *cap = new_cap;
    }
    va_start(args, fmt);
    vsnprintf(*buffer + *len, *cap - *len, fmt, args);
    va_end(args);
    *len += (size_t)needed;
    return 0;
}

static void json_escape_string(const char *src, char *dest, size_t dest_size) {
    if (!dest || dest_size == 0) {
        return;
    }
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t pos = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && pos + 1 < dest_size; ++p) {
        unsigned char ch = *p;
        if (ch == '"' || ch == '\\') {
            if (pos + 2 >= dest_size) {
                break;
            }
            dest[pos++] = '\\';
            dest[pos++] = (char)ch;
        } else if (ch < 0x20) {
            if (pos + 6 >= dest_size) {
                break;
            }
            int written = snprintf(dest + pos, dest_size - pos, "\\u%04x", ch);
            if (written < 0) {
                break;
            }
            pos += (size_t)written;
        } else {
            dest[pos++] = (char)ch;
        }
    }
    dest[pos] = '\0';
}

static int parse_json_string_field(const char *json,
                                   const char *field,
                                   char *out,
                                   size_t out_size) {
    if (!json || !field || !out || out_size == 0) {
        return -1;
    }
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", field);
    const char *pos = strstr(json, needle);
    if (!pos) {
        return -1;
    }
    pos += strlen(needle);
    while (*pos && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (*pos != ':') {
        return -1;
    }
    pos++;
    while (*pos && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (*pos != '"') {
        return -1;
    }
    pos++;
    size_t len = 0;
    while (*pos && *pos != '"') {
        if (*pos == '\\' && pos[1] != '\0') {
            pos++;
        }
        if (len + 1 >= out_size) {
            return -1;
        }
        out[len++] = *pos++;
    }
    if (*pos != '"') {
        return -1;
    }

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


static const char *skip_ws(const char *ptr, const char *end) {
    while (ptr < end && isspace((unsigned char)*ptr)) {
        ++ptr;
    }
    return ptr;
}

static int parse_bytecode_count(const char *body, size_t body_len, size_t *out_count) {
    if (!body || !out_count) {
        return -1;
    }
    if (body_len == 0) {
        body_len = strlen(body);
    }
    const char *end = body + body_len;
    const char key[] = "\"bytecode\"";
    const char *key_pos = memmem_const(body, body_len, key, sizeof(key) - 1);
    if (!key_pos) {
        return -1;
    }
    const char *ptr = key_pos + (sizeof(key) - 1);
    ptr = skip_ws(ptr, end);
    if (ptr >= end || *ptr != ':') {
        return -1;
    }
    ++ptr;
    ptr = skip_ws(ptr, end);
    if (ptr >= end || *ptr != '[') {
        return -1;
    }
    ++ptr;
    size_t count = 0;
    int expect_value = 1;
    while (ptr < end) {
        ptr = skip_ws(ptr, end);
        if (ptr >= end) {
            break;
        }
        if (*ptr == ']') {
            if (expect_value && count == 0) {
                return -1;
            }
            ++ptr;
            *out_count = count;
            return 0;
        }
        if (!expect_value) {
            if (*ptr != ',') {
                return -1;
            }
            ++ptr;
            expect_value = 1;
            continue;
        }
        if (!isdigit((unsigned char)*ptr)) {
            return -1;
        }
        while (ptr < end && isdigit((unsigned char)*ptr)) {
            ++ptr;
        }
        ++count;
        expect_value = 0;
    }
    return -1;
}

static int json_unescape_into_buffer(const char *start,
                                     const char *end,
                                     char *out,
                                     size_t out_len) {
    size_t written = 0;
    while (start < end) {
        if (written + 1 >= out_len) {
            return -1;
        }
        unsigned char ch = (unsigned char)*start++;
        if (ch == '\\') {
            if (start >= end) {
                return -1;
            }
            unsigned char esc = (unsigned char)*start++;
            switch (esc) {
                case '"':
                case '\\':
                case '/':
                    ch = esc;
                    break;
                case 'b':
                    ch = '\b';
                    break;
                case 'f':
                    ch = '\f';
                    break;
                case 'n':
                    ch = '\n';
                    break;
                case 'r':
                    ch = '\r';
                    break;
                case 't':
                    ch = '\t';
                    break;
                default:
                    return -1;
            }
        }
        out[written++] = (char)ch;
    }
    out[written] = '\0';
    return 0;
}

static int extract_string_field(const char *body, size_t body_len, const char *key, char *out, size_t out_len) {
    if (!body || !key || !out || out_len == 0) {
        return -1;
    }
    if (body_len == 0) {
        body_len = strlen(body);
    }
    const char *end = body + body_len;
    size_t key_len = strlen(key);
    const char *key_pos = memmem_const(body, body_len, key, key_len);
    if (!key_pos) {
        return -1;
    }
    const char *ptr = key_pos + key_len;
    ptr = skip_ws(ptr, end);
    if (ptr >= end || *ptr != ':') {
        return -1;
    }
    ++ptr;
    ptr = skip_ws(ptr, end);
    if (ptr >= end || *ptr != '"') {
        return -1;
    }
    ++ptr;
    const char *value_start = ptr;
    while (ptr < end) {
        if (*ptr == '\\') {
            ++ptr;
            if (ptr >= end) {
                return -1;
            }
            ++ptr;
            continue;
        }
        if (*ptr == '"') {
            break;
        }
        ++ptr;
    }
    if (ptr >= end || *ptr != '"') {
        return -1;
    }
    const char *value_end = ptr;
    return json_unescape_into_buffer(value_start, value_end, out, out_len);
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

static int parse_json_uint_field(const char *json, const char *field, uint64_t *out) {
    if (!json || !field || !out) {
        return -1;
    }
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", field);
    const char *pos = strstr(json, needle);
    if (!pos) {
        return -1;
    }
    pos += strlen(needle);
    while (*pos && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (*pos != ':') {
        return -1;
    }
    pos++;
    while (*pos && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (!isdigit((unsigned char)*pos)) {
        return -1;
    }
    char *end = NULL;
    unsigned long long value = strtoull(pos, &end, 10);
    if (end == pos) {
        return -1;
    }
    *out = (uint64_t)value;
    return 0;
}

static int parse_program_array(const char *json, uint8_t **out_code, size_t *out_len) {
    if (!json || !out_code || !out_len) {
        return -1;
    }
    *out_code = NULL;
    *out_len = 0;
    const char *needle = "\"program\"";
    const char *pos = strstr(json, needle);
    if (!pos) {
        return -1;
    }
    pos += strlen(needle);
    while (*pos && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (*pos != ':') {
        return -1;
    }
    pos++;
    while (*pos && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (*pos != '[') {
        return -1;
    }
    pos++;
    size_t cap = 16;
    size_t len = 0;
    uint8_t *code = malloc(cap);
    if (!code) {
        return -1;
    }
    while (*pos) {
        while (*pos && isspace((unsigned char)*pos)) {
            pos++;
        }
        if (*pos == ']') {
            pos++;
            break;
        }
        char *end = NULL;
        long value = strtol(pos, &end, 10);
        if (end == pos || value < 0 || value > 255) {
            free(code);
            return -1;
        }
        if (len >= cap) {
            size_t new_cap = cap * 2;
            uint8_t *tmp = realloc(code, new_cap);
            if (!tmp) {
                free(code);
                return -1;
            }
            code = tmp;
            cap = new_cap;
        }
        code[len++] = (uint8_t)value;
        pos = end;
        while (*pos && isspace((unsigned char)*pos)) {
            pos++;
        }
        if (*pos == ',') {
            pos++;
        }
    }
    if (len == 0) {
        free(code);
        return -1;
    }
    *out_code = code;
    *out_len = len;
    return 0;
}

static int digits_from_number(uint64_t value, uint8_t *out, size_t capacity, size_t *out_len) {
    if (!out || !out_len || capacity == 0) {
        return -1;
    }
    size_t len = 0;
    do {
        if (len >= capacity) {
            return -1;
        }
        out[len++] = (uint8_t)(value % 10u);
        value /= 10u;
    } while (value > 0);
    for (size_t i = 0; i < len / 2; ++i) {
        uint8_t tmp = out[i];
        out[i] = out[len - 1 - i];
        out[len - 1 - i] = tmp;
    }
    *out_len = len;
    return 0;
}

static void ai_record_dialog(const char *prompt, double reward, int success, double expected_result) {
    if (!routes_ai || !prompt) {
        return;
    }
    KolibriAISelfplayInteraction interaction = {0};
    interaction.task.difficulty = 1;
    snprintf(interaction.task.description,
             sizeof(interaction.task.description),
             "http:%s",
             prompt);
    interaction.task.expected_result = expected_result;
    interaction.predicted_result = expected_result;
    interaction.error = success ? 0.0 : 1.0;
    interaction.reward = reward;
    interaction.success = success;
    kolibri_ai_record_interaction(routes_ai, &interaction);
    kolibri_ai_process_iteration(routes_ai);
}

static int handle_dialog(const kolibri_config_t *cfg,
                         const char *body,
                         size_t body_len,
                         http_response_t *resp) {
    (void)body_len;
    if (!cfg || !resp) {
        return -1;
    }
    char input[512];
    if (parse_json_string_field(body, "input", input, sizeof(input)) != 0) {
        return respond_json(resp, "{\"error\":\"invalid_payload\"}", 400);
    }

    uint8_t *bytecode = NULL;
    size_t bytecode_len = 0;
    vm_result_t result = {0};
    int evaluated = 0;
    if (formula_vm_compile_from_text(input, &bytecode, &bytecode_len) == 0) {
        prog_t prog = {.code = bytecode, .len = bytecode_len};
        vm_limits_t limits = {.max_steps = cfg->vm.max_steps, .max_stack = cfg->vm.max_stack};
        if (limits.max_steps == 0) {
            limits.max_steps = 256;
        }
        if (limits.max_stack == 0) {
            limits.max_stack = 64;
        }
        if (vm_run(&prog, &limits, NULL, &result) == 0 && result.status == VM_OK) {
            evaluated = 1;
        }
        free(bytecode);
    }

    if (evaluated) {
        uint8_t key_digits[32];
        size_t key_len = 0;
        uint8_t val_digits[32];
        size_t val_len = 0;
        int stored = 0;
        pthread_mutex_lock(&dialog_lock);
        uint64_t exchange = ++dialog_exchange_counter;
        pthread_mutex_unlock(&dialog_lock);
        if (digits_from_number(exchange, key_digits, sizeof(key_digits), &key_len) == 0 &&
            digits_from_number(result.result, val_digits, sizeof(val_digits), &val_len) == 0) {
            if (fkv_put(key_digits, key_len, val_digits, val_len, FKV_ENTRY_TYPE_VALUE) == 0) {
                stored = 1;
            }
        }
        ai_record_dialog(input, 1.0, 1, (double)result.result);
        char buffer[256];
        snprintf(buffer,
                 sizeof(buffer),
                 "{\"answer\":\"%llu\",\"status\":\"vm\",\"steps\":%u,\"stored\":%s}",
                 (unsigned long long)result.result,
                 result.steps,
                 stored ? "true" : "false");
        return respond_json(resp, buffer, 200);
    }

    const char *fallback = "Kolibri is still synthesizing an answer";
    char answer[256];
    if (routes_ai) {
        Formula *best = kolibri_ai_get_best_formula(routes_ai);
        if (best) {
            if (best->representation == FORMULA_REPRESENTATION_TEXT && best->content[0]) {
                char escaped[192];
                json_escape_string(best->content, escaped, sizeof(escaped));
                snprintf(answer, sizeof(answer), "{\"answer\":\"%s\",\"status\":\"knowledge\"}", escaped);
                formula_clear(best);
                free(best);
                ai_record_dialog(input, 0.3, 0, 0.0);
                return respond_json(resp, answer, 200);
            }
            formula_clear(best);
            free(best);
        }
    }
    ai_record_dialog(input, 0.2, 0, 0.0);
    snprintf(answer,
             sizeof(answer),
             "{\"answer\":\"%s\",\"status\":\"pending\"}",
             fallback);
    return respond_json(resp, answer, 200);

}

static int parse_vm_program(const char *body,
                            uint8_t **out_code,
                            size_t *out_len,
                            char *error,
                            size_t error_size) {
    if (!body || !out_code || !out_len) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "missing body");
        }
        return -1;
    }

    char program_expr[256];
    if (json_extract_string(body, "program", program_expr, sizeof(program_expr)) == 0 ||
        json_extract_string(body, "formula", program_expr, sizeof(program_expr)) == 0 ||
        json_extract_string(body, "expression", program_expr, sizeof(program_expr)) == 0) {
        if (formula_vm_compile_from_text(program_expr, out_code, out_len) != 0) {
            if (error && error_size > 0) {
                snprintf(error, error_size, "unable to compile program");
            }
            return -1;
        }
        return 0;
    }

    if (json_extract_uint8_array(body, "bytecode", out_code, out_len) == 0) {
        return 0;
    }

    if (parse_program_array(body, out_code, out_len) == 0) {
        return 0;
    }

    if (error && error_size > 0) {
        snprintf(error, error_size, "program field missing");
    }
    return -1;
}

static int handle_vm_run(const kolibri_config_t *cfg,
                         const char *body,
                         size_t body_len,
                         http_response_t *resp) {
    (void)body_len;
    if (!cfg || !resp) {
        return -1;
    }

    char error[64];
    uint8_t *program = NULL;
    size_t program_len = 0;
    if (parse_vm_program(body, &program, &program_len, error, sizeof(error)) != 0) {
        return respond_error(resp, 400, "bad_request", error);
    }

    vm_limits_t limits = {
        .max_steps = cfg->vm.max_steps ? cfg->vm.max_steps : 512,
        .max_stack = cfg->vm.max_stack ? cfg->vm.max_stack : 128,
    };

    uint64_t override = 0;
    uint32_t gas_limit = 0;
    if (parse_json_uint_field(body, "max_steps", &override) == 0 && override > 0) {
        limits.max_steps = (uint32_t)override;
    }
    if (parse_json_uint_field(body, "max_stack", &override) == 0 && override > 0) {
        limits.max_stack = (uint32_t)override;
    }
    if (json_extract_uint32(body, "gas_limit", &gas_limit) == 0 && gas_limit > 0) {
        limits.max_steps = gas_limit;
    }
    if (limits.max_steps == 0) {
        limits.max_steps = 256;
    }
    if (limits.max_stack == 0) {
        limits.max_stack = 64;
    }

    prog_t prog = {.code = program, .len = program_len};
    vm_result_t result = {0};
    int rc = vm_run(&prog, &limits, NULL, &result);
    free(program);
    if (rc != 0 || result.status != VM_OK) {
        return respond_error(resp, 400, "vm_error", "virtual machine rejected program");
    }

    char buffer[256];
    snprintf(buffer,
             sizeof(buffer),
             "{\"result\":\"%llu\",\"stack\":[\"%llu\"],\"trace\":{\"steps\":[]},\"gas_used\":%u}",
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
        const char *param_end = strchr(query, '&');
        if (!param_end) {
            param_end = query + strlen(query);
        }
        if ((size_t)(param_end - query) > name_len && strncmp(query, name, name_len) == 0 &&
            query[name_len] == '=') {
            const char *value_start = query + name_len + 1;
            size_t value_len = (size_t)(param_end - value_start);
            if (value_len == 0) {
                return -1;
            }
            if (value_len >= out_size) {
                value_len = out_size - 1;
            }
            memcpy(out, value_start, value_len);
            out[value_len] = '\0';
            return 0;
        }
        if (*param_end == '\0') {
            break;
        }
        query = param_end + 1;
    }
    return -1;
}

static char hex_from_digit(uint8_t digit) {
    return (char)((digit < 10) ? ('0' + digit) : ('a' + (digit - 10)));
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
        if (entry->type != FKV_ENTRY_TYPE_VALUE) {
            continue;
        }
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
    struct json_object *obj = json_object_new_object();
    if (!obj) {
        return -1;
    }
    json_object_object_add(obj, "status", json_object_new_string("ok"));
    json_object_object_add(obj, "uptime_ms", json_object_new_int64((int64_t)uptime_ms));
    json_object_object_add(obj, "blockchain_attached", json_object_new_boolean(routes_blockchain != NULL));
    json_object_object_add(obj, "requests_total", json_object_new_int64((int64_t)routes_metrics.total_requests));
    return respond_with_json_object(resp, obj, 200);
}

static void iso8601_from_time(time_t value, char *buffer, size_t buffer_len) {
    if (!buffer || buffer_len == 0) {
        return;
    }
    if (value <= 0) {
        buffer[0] = '\0';
        return;
    }
    struct tm tm_utc;
    gmtime_r(&value, &tm_utc);
    strftime(buffer, buffer_len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

static int handle_metrics(http_response_t *resp) {

    struct json_object *obj = json_object_new_object();
    if (!obj) {
        return -1;
    }
    json_object_object_add(obj, "requests", json_object_new_int64((int64_t)routes_metrics.total_requests));
    json_object_object_add(obj, "errors", json_object_new_int64((int64_t)routes_metrics.total_errors));
    json_object_object_add(obj, "vmRuns", json_object_new_int64((int64_t)routes_metrics.vm_runs));
    json_object_object_add(obj, "fkvQueries", json_object_new_int64((int64_t)routes_metrics.fkv_queries));
    json_object_object_add(obj, "programSubmissions",
                           json_object_new_int64((int64_t)routes_metrics.program_submissions));
    json_object_object_add(obj, "chainSubmissions",
                           json_object_new_int64((int64_t)routes_metrics.chain_submissions));

    if (routes_blockchain) {
        json_object_object_add(obj, "blocks",
                               json_object_new_int64((int64_t)routes_blockchain->block_count));
    } else {
        json_object_object_add(obj, "blocks", json_object_new_int64(0));
    }
    json_object_object_add(obj, "tasksInFlight", json_object_new_int64(0));

    char iso[32];
    iso8601_from_time(routes_metrics.last_block_time, iso, sizeof(iso));
    if (iso[0] != '\0') {
        json_object_object_add(obj, "lastBlockTime", json_object_new_string(iso));
    } else {
        json_object_object_add(obj, "lastBlockTime", json_object_new_null());
    }

    return respond_with_json_object(resp, obj, 200);

    if (!resp) {
        return -1;
    }
    if (!routes_ai) {
        return respond_json(resp, "{\"requests\":0,\"errors\":0}", 200);
    }
    char *ai_state = kolibri_ai_serialize_state(routes_ai);
    if (!ai_state) {
        return respond_json(resp, "{\"requests\":0,\"errors\":0}", 200);
    }
    size_t len = strlen(ai_state) + 64;
    char *buffer = malloc(len);
    if (!buffer) {
        free(ai_state);
        return respond_json(resp, "{\"requests\":0,\"errors\":0}", 200);
    }
    snprintf(buffer, len, "{\"requests\":0,\"errors\":0,\"ai\":%s}", ai_state);
    free(ai_state);
    int rc = respond_json(resp, buffer, 200);
    free(buffer);
    return rc;
}

typedef int (*route_handler_fn)(const kolibri_config_t *cfg,
                                const char *path,
                                const char *body,
                                size_t body_len,
                                http_response_t *resp);


static int route_handle_health(const kolibri_config_t *cfg,
                               const char *path,
                               const char *body,
                               size_t body_len,
                               http_response_t *resp) {
    (void)cfg;
    (void)path;
    (void)body;
    (void)body_len;
    return handle_health(resp);

}

static int route_handle_metrics(const kolibri_config_t *cfg,
                                const char *path,
                                const char *body,
                                size_t body_len,
                                http_response_t *resp) {
    (void)cfg;
    (void)path;
    (void)body;
    (void)body_len;
    return handle_metrics(resp);
}

static int route_handle_fkv_get(const kolibri_config_t *cfg,
                                const char *path,
                                const char *body,
                                size_t body_len,
                                http_response_t *resp) {
    (void)cfg;
    (void)body;
    (void)body_len;

    const char *reply_message = "Kolibri is online";
    if (body && strstr(body, "ping")) {
        reply_message = "pong";
    }
    struct json_object *obj = json_object_new_object();
    if (!obj) {
        return -1;
    }
    json_object_object_add(obj, "answer", json_object_new_string(reply_message));
    return respond_with_json_object(resp, obj, 200);
}

static int parse_program_array(struct json_object *value, uint8_t **out_code, size_t *out_len) {
    if (!value || !out_code || !out_len) {
        return -1;
    }
    *out_code = NULL;
    *out_len = 0;

    size_t length = (size_t)json_object_array_length(value);
    if (length == 0) {
        return -1;
    }
    uint8_t *code = calloc(length, sizeof(uint8_t));
    if (!code) {
        return -1;
    }
    for (size_t i = 0; i < length; ++i) {
        struct json_object *entry = json_object_array_get_idx(value, (int)i);
        if (!entry) {
            free(code);
            return -1;
        }
        if (!json_object_is_type(entry, json_type_int)) {
            free(code);
            return -1;
        }
        int64_t val = json_object_get_int64(entry);
        if (val < 0) {
            val = 0;
        }
        if (val > 255) {
            val = 255;
        }
        code[i] = (uint8_t)val;
    }
    *out_code = code;
    *out_len = length;
    return 0;
}

static int parse_program_from_json(struct json_object *root,
                                   uint8_t **out_code,
                                   size_t *out_len) {
    if (!root) {
        return -1;
    }
    struct json_object *program_value = NULL;
    if (!json_object_object_get_ex(root, "program", &program_value)) {
        if (!json_object_object_get_ex(root, "bytecode", &program_value)) {
            return -1;
        }
    }
    if (json_object_is_type(program_value, json_type_array)) {
        return parse_program_array(program_value, out_code, out_len);
    }
    if (json_object_is_type(program_value, json_type_string)) {
        const char *raw = json_object_get_string(program_value);
        if (!raw) {
            return -1;
        }
        struct json_object *temp = json_object_new_array();
        if (!temp) {
            return -1;
        }
        const char *ptr = raw;
        while (*ptr) {
            while (*ptr && (isspace((unsigned char)*ptr) || *ptr == ',' || *ptr == ';')) {
                ptr++;
            }
            if (!*ptr) {
                break;
            }
            char *end = NULL;
            long value = strtol(ptr, &end, 10);
            if (end == ptr) {
                json_object_put(temp);
                return -1;
            }
            json_object_array_add(temp, json_object_new_int((int)value));
            ptr = end;
        }
        int rc = parse_program_array(temp, out_code, out_len);
        json_object_put(temp);
        return rc;
    }
    return -1;
}

static double compute_program_poe(const vm_result_t *result, size_t program_len) {
    if (!result || result->status != VM_OK || !result->halted) {
        return 0.0;
    }
    double steps_penalty = 1.0 / (1.0 + (double)result->steps / 16.0);
    double magnitude = log1p((double)result->result);
    double magnitude_norm = magnitude / (magnitude + 4.0);
    double poe = steps_penalty * magnitude_norm;
    if (program_len > 0) {
        double brevity_bonus = 1.0 / (1.0 + (double)program_len / 32.0);
        poe = fmin(1.0, poe * 0.7 + brevity_bonus * 0.3);
    }
    if (poe < 0.0) {
        poe = 0.0;
    }
    if (poe > 1.0) {
        poe = 1.0;
    }
    return poe;
}

static double compute_program_mdl(size_t program_len) {
    if (program_len == 0) {
        return 0.0;
    }
    double scaled = log1p((double)program_len);
    double denom = log1p(512.0);
    if (denom <= 0.0) {
        denom = 1.0;
    }
    double mdl = scaled / denom;
    if (mdl > 1.0) {
        mdl = 1.0;
    }
    if (mdl < 0.0) {
        mdl = 0.0;
    }
    return mdl;
}

static void append_trace_entry(struct json_object *array, const vm_trace_entry_t *entry) {
    if (!array || !entry) {
        return;
    }
    struct json_object *obj = json_object_new_object();
    if (!obj) {
        return;
    }
    json_object_object_add(obj, "step", json_object_new_int((int)entry->step));
    json_object_object_add(obj, "ip", json_object_new_int((int)entry->ip));
    json_object_object_add(obj, "opcode", json_object_new_int((int)entry->opcode));
    json_object_object_add(obj, "stack_top", json_object_new_int64((int64_t)entry->stack_top));
    json_object_object_add(obj, "gas_left", json_object_new_int((int)entry->gas_left));
    json_object_array_add(array, obj);
}

static int handle_vm_run(const kolibri_config_t *cfg,
                         const char *body,
                         size_t body_len,
                         http_response_t *resp) {
    (void)body_len;
    if (!cfg) {
        return -1;
    }
    struct json_tokener *tok = json_tokener_new();
    if (!tok) {
        return -1;
    }
    enum json_tokener_error jerr = json_tokener_success;
    struct json_object *root = json_tokener_parse_ex(tok, body ? body : "{}", (int)(body ? body_len : 0));
    jerr = json_tokener_get_error(tok);
    json_tokener_free(tok);
    if (jerr != json_tokener_success || !root || !json_object_is_type(root, json_type_object)) {
        if (root) {
            json_object_put(root);
        }
        return respond_with_json_object(resp, make_error_json("invalid_json"), 400);
    }

    uint8_t *code = NULL;
    size_t code_len = 0;
    if (parse_program_from_json(root, &code, &code_len) != 0) {
        json_object_put(root);
        return respond_with_json_object(resp, make_error_json("invalid_program"), 400);
    }

    vm_limits_t limits = {cfg->vm.max_steps ? cfg->vm.max_steps : 1024,
                          cfg->vm.max_stack ? cfg->vm.max_stack : 128};
    struct json_object *gas_obj = NULL;
    if (json_object_object_get_ex(root, "gas_limit", &gas_obj) && json_object_is_type(gas_obj, json_type_int)) {
        limits.max_steps = (uint32_t)json_object_get_int(gas_obj);
    }
    json_object_put(root);

    prog_t program = {code, code_len};
    vm_trace_t trace = {0};
    vm_trace_entry_t *entries = NULL;
    size_t trace_capacity = cfg->vm.trace_depth ? cfg->vm.trace_depth : 32;
    if (trace_capacity > 0) {
        entries = calloc(trace_capacity, sizeof(vm_trace_entry_t));
        if (entries) {
            trace.entries = entries;
            trace.capacity = trace_capacity;
            trace.count = 0;
        }
    }

    vm_result_t result = {0};
    int rc = vm_run(&program, &limits, entries ? &trace : NULL, &result);
    free(code);
    if (rc != 0) {
        free(entries);
        return respond_with_json_object(resp, make_error_json("vm_failure"), 500);
    }

    struct json_object *json = json_object_new_object();
    if (!json) {
        free(entries);
        return -1;
    }

    json_object_object_add(json, "status", json_object_new_string(vm_status_to_string(result.status)));
    json_object_object_add(json, "result", json_object_new_int64((int64_t)result.result));
    json_object_object_add(json, "steps", json_object_new_int((int)result.steps));
    json_object_object_add(json, "halted", json_object_new_boolean(result.halted));
    json_object_object_add(json, "gasUsed", json_object_new_int((int)result.steps));

    struct json_object *trace_array = json_object_new_array();
    if (trace_array) {
        for (size_t i = 0; i < trace.count; ++i) {
            append_trace_entry(trace_array, &trace.entries[i]);
        }
        json_object_object_add(json, "trace", trace_array);
    }
    free(entries);

    double poe_value = compute_program_poe(&result, program.len);
    double mdl_value = compute_program_mdl(program.len);
    struct json_object *poe_obj = json_object_new_double(poe_value);
    struct json_object *mdl_obj = json_object_new_double(mdl_value);
    if (poe_obj && mdl_obj) {
        json_object_object_add(json, "poe", poe_obj);
        json_object_object_add(json, "mdl", mdl_obj);
    } else {
        if (poe_obj) {
            json_object_put(poe_obj);
        }
        if (mdl_obj) {
            json_object_put(mdl_obj);
        }
    }
    json_object_object_add(json, "PoE", json_object_new_double(poe_value));
    json_object_object_add(json, "MDL", json_object_new_double(mdl_value));

    routes_metrics.vm_runs++;

    return respond_with_json_object(resp, json, 200);
}

static void update_metrics_after_block(void) {
    if (!routes_blockchain || routes_blockchain->block_count == 0) {
        return;
    }
    Block *last = routes_blockchain->blocks[routes_blockchain->block_count - 1];
    if (last) {
        routes_metrics.last_block_time = last->timestamp;
    }
}

static void persist_program_record(const char *id,
                                   const uint8_t *code,
                                   size_t len,
                                   double poe,
                                   double mdl,
                                   double score,
                                   int accepted) {
    if (!id || !code || len == 0) {
        return;
    }
    size_t capacity = sizeof(program_records) / sizeof(program_records[0]);
    if (program_record_count >= capacity) {
        program_record_reset(&program_records[program_record_count - 1]);
        program_record_count--;
    }
    if (program_record_count > 0) {
        memmove(&program_records[1], &program_records[0], program_record_count * sizeof(program_record_t));
    }
    program_record_t *slot = &program_records[0];
    memset(slot, 0, sizeof(*slot));
    strncpy(slot->id, id, sizeof(slot->id) - 1);
    slot->poe = poe;
    slot->mdl = mdl;
    slot->score = score;
    slot->accepted = accepted;
    slot->submitted_at = time(NULL);
    slot->bytecode = calloc(len, sizeof(uint8_t));
    if (slot->bytecode) {
        memcpy(slot->bytecode, code, len);
        slot->bytecode_len = len;
    }
    if (program_record_count < sizeof(program_records) / sizeof(program_records[0])) {
        program_record_count++;
    }
}

static int handle_program_submit(const kolibri_config_t *cfg,
                                 const char *body,
                                 size_t body_len,
                                 http_response_t *resp) {
    if (!routes_blockchain) {
        return respond_with_json_object(resp, make_error_json("blockchain_unavailable"), 503);
    }
    (void)cfg;
    struct json_tokener *tok = json_tokener_new();
    if (!tok) {
        return -1;
    }
    struct json_object *root = json_tokener_parse_ex(tok, body ? body : "{}", (int)(body ? body_len : 0));
    enum json_tokener_error jerr = json_tokener_get_error(tok);
    json_tokener_free(tok);
    if (jerr != json_tokener_success || !root || !json_object_is_type(root, json_type_object)) {
        if (root) {
            json_object_put(root);
        }
        return respond_with_json_object(resp, make_error_json("invalid_json"), 400);
    }

    uint8_t *code = NULL;
    size_t len = 0;
    if (parse_program_from_json(root, &code, &len) != 0) {
        json_object_put(root);
        return respond_with_json_object(resp, make_error_json("invalid_program"), 400);
    }

    prog_t program = {code, len};
    vm_limits_t limits = {256, 128};
    vm_result_t result = {0};
    if (vm_run(&program, &limits, NULL, &result) != 0) {
        free(code);
        json_object_put(root);
        return respond_with_json_object(resp, make_error_json("vm_failure"), 500);
    }

    double poe = compute_program_poe(&result, len);
    double mdl = compute_program_mdl(len);
    double score = poe - mdl;
    int accepted = (result.status == VM_OK);

    char program_id[64];
    snprintf(program_id, sizeof(program_id), "program-%llu", (unsigned long long)(++program_id_counter));

    Formula formula;
    memset(&formula, 0, sizeof(formula));
    strncpy(formula.id, program_id, sizeof(formula.id) - 1);
    formula.effectiveness = poe;
    formula.created_at = time(NULL);
    formula.tests_passed = (uint32_t)(accepted ? 1 : 0);
    formula.confirmations = 0;
    formula.representation = FORMULA_REPRESENTATION_TEXT;
    formula.type = FORMULA_LINEAR;
    size_t written = 0;
    for (size_t i = 0; i < len && written + 3 < sizeof(formula.content); ++i) {
        int n = snprintf(formula.content + written,
                         sizeof(formula.content) - written,
                         i + 1 == len ? "%u" : "%u,",
                         (unsigned)code[i]);
        if (n < 0) {
            break;
        }
        written += (size_t)n;
    }

    Formula *formula_ptr = &formula;
    if (!blockchain_add_block(routes_blockchain, &formula_ptr, 1)) {
        free(code);
        json_object_put(root);
        return respond_with_json_object(resp, make_error_json("blockchain_append_failed"), 500);
    }
    update_metrics_after_block();

    persist_program_record(program_id, code, len, poe, mdl, score, accepted);

    struct json_object *response = json_object_new_object();
    json_object_object_add(response, "programId", json_object_new_string(program_id));
    json_object_object_add(response, "poe", json_object_new_double(poe));
    json_object_object_add(response, "mdl", json_object_new_double(mdl));
    json_object_object_add(response, "score", json_object_new_double(score));
    json_object_object_add(response, "accepted", json_object_new_boolean(accepted));
    json_object_object_add(response, "PoE", json_object_new_double(poe));
    json_object_object_add(response, "MDL", json_object_new_double(mdl));

    const char *notes = NULL;
    struct json_object *notes_obj = NULL;
    if (json_object_object_get_ex(root, "notes", &notes_obj) && json_object_is_type(notes_obj, json_type_string)) {
        notes = json_object_get_string(notes_obj);
    }
    if (notes) {
        json_object_object_add(response, "notes", json_object_new_string(notes));
    }
    json_object_put(root);
    free(code);

    routes_metrics.program_submissions++;

    return respond_with_json_object(resp, response, accepted ? 200 : 202);
}

static char *digits_to_string(const uint8_t *digits, size_t len) {
    if (!digits || len == 0) {
        return NULL;
    }
    char *buf = calloc(len + 1, 1);
    if (!buf) {
        return NULL;
    }
    for (size_t i = 0; i < len; ++i) {
        if (digits[i] <= 9) {
            buf[i] = (char)('0' + digits[i]);
        } else if (isprint(digits[i])) {
            buf[i] = (char)digits[i];
        } else {
            buf[i] = '?';
        }
    }
    buf[len] = '\0';
    return buf;
}

static int prefix_to_digits(const char *prefix, uint8_t **out_digits, size_t *out_len) {
    if (!prefix || !out_digits || !out_len) {
        return -1;
    }
    size_t len = strlen(prefix);
    if (len == 0) {
        return -1;
    }
    uint8_t *digits = calloc(len, sizeof(uint8_t));
    if (!digits) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!isdigit((unsigned char)prefix[i])) {
            free(digits);
            return -1;
        }
        digits[i] = (uint8_t)(prefix[i] - '0');
    }
    *out_digits = digits;
    *out_len = len;
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

static int parse_query_param(const char *query,
                             const char *key,
                             char *out,
                             size_t out_len) {
    if (!query || !key || !out || out_len == 0) {

        return -1;
    }
    size_t key_len = strlen(key);
    const char *ptr = query;
    while (*ptr) {
        while (*ptr == '&') {
            ptr++;
        }
        if (!*ptr) {
            break;
        }
        const char *eq = strchr(ptr, '=');
        const char *next = strchr(ptr, '&');
        if (!next) {
            next = ptr + strlen(ptr);
        }
        if (eq && (size_t)(eq - ptr) == key_len && strncmp(ptr, key, key_len) == 0) {
            size_t value_len = (size_t)(next - (eq + 1));
            if (value_len >= out_len) {
                value_len = out_len - 1;
            }
            memcpy(out, eq + 1, value_len);
            out[value_len] = '\0';
            return 0;
        }
        ptr = next;
    }
    return -1;
}

static int handle_fkv_get(const char *path, http_response_t *resp) {
    const char *query = strchr(path, '?');
    if (!query || !*(query + 1)) {
        return respond_with_json_object(resp, make_error_json("missing_prefix"), 400);
    }
    query++;
    char prefix[128];
    if (parse_query_param(query, "prefix", prefix, sizeof(prefix)) != 0) {
        return respond_with_json_object(resp, make_error_json("missing_prefix"), 400);
    }
    char limit_buf[16];
    size_t limit = 20;
    if (parse_query_param(query, "limit", limit_buf, sizeof(limit_buf)) == 0) {
        long parsed = strtol(limit_buf, NULL, 10);
        if (parsed > 0 && parsed < 1000) {
            limit = (size_t)parsed;
        }
    }

    uint8_t *digits = NULL;
    size_t digits_len = 0;
    if (prefix_to_digits(prefix, &digits, &digits_len) != 0) {
        return respond_with_json_object(resp, make_error_json("invalid_prefix"), 400);
    }

    fkv_iter_t it = {0};
    if (fkv_get_prefix(digits, digits_len, &it, limit) != 0) {
        free(digits);
        return respond_with_json_object(resp, make_error_json("fkv_error"), 500);
    }
    free(digits);

    struct json_object *response = json_object_new_object();
    struct json_object *values = json_object_new_array();
    struct json_object *programs = json_object_new_array();
    struct json_object *topk = json_object_new_array();
    if (!response || !values || !programs || !topk) {
        if (response) json_object_put(response);
        if (values) json_object_put(values);
        if (programs) json_object_put(programs);
        if (topk) json_object_put(topk);
        fkv_iter_free(&it);
        return -1;
    }

    for (size_t i = 0; i < it.count; ++i) {
        fkv_entry_t *entry = &it.entries[i];
        char *key_str = digits_to_string(entry->key, entry->key_len);
        char *value_str = digits_to_string(entry->value, entry->value_len);
        struct json_object *obj = json_object_new_object();
        json_object_object_add(obj, "key", json_object_new_string(key_str ? key_str : ""));
        json_object_object_add(obj, "value", json_object_new_string(value_str ? value_str : ""));
        json_object_array_add(values, obj);
        free(key_str);
        free(value_str);
    }
    fkv_iter_free(&it);

    size_t max_programs = program_record_count < 5 ? program_record_count : 5;
    for (size_t i = 0; i < max_programs; ++i) {
        const program_record_t *rec = &program_records[i];
        struct json_object *obj = json_object_new_object();
        json_object_object_add(obj, "id", json_object_new_string(rec->id));
        json_object_object_add(obj, "score", json_object_new_double(rec->score));
        json_object_object_add(obj, "poe", json_object_new_double(rec->poe));
        json_object_object_add(obj, "mdl", json_object_new_double(rec->mdl));
        struct json_object *bytecode = json_object_new_array();
        if (bytecode) {
            for (size_t j = 0; j < rec->bytecode_len; ++j) {
                json_object_array_add(bytecode, json_object_new_int(rec->bytecode[j]));
            }
            json_object_object_add(obj, "bytecode", bytecode);
        }
        json_object_array_add(programs, obj);
    }

    json_object_object_add(response, "values", values);
    json_object_object_add(response, "programs", programs);
    json_object_object_add(response, "topk", topk);

    routes_metrics.fkv_queries++;

    return respond_with_json_object(resp, response, 200);
}

static int handle_chain_submit(const char *body, size_t body_len, http_response_t *resp) {
    (void)body_len;
    if (!routes_blockchain) {
        return respond_with_json_object(resp, make_error_json("blockchain_unavailable"), 503);
    }
    struct json_tokener *tok = json_tokener_new();
    if (!tok) {
        return -1;
    }
    struct json_object *root = json_tokener_parse_ex(tok, body ? body : "{}", (int)(body ? body_len : 0));
    enum json_tokener_error jerr = json_tokener_get_error(tok);
    json_tokener_free(tok);
    if (jerr != json_tokener_success || !root || !json_object_is_type(root, json_type_object)) {
        if (root) {
            json_object_put(root);
        }
        return respond_with_json_object(resp, make_error_json("invalid_json"), 400);
    }

    struct json_object *program_id_obj = NULL;
    if (!json_object_object_get_ex(root, "program_id", &program_id_obj) &&
        !json_object_object_get_ex(root, "programId", &program_id_obj)) {
        json_object_put(root);
        return respond_with_json_object(resp, make_error_json("missing_program_id"), 400);
    }
    const char *program_id = json_object_get_string(program_id_obj);
    program_record_t *record = program_record_find(program_id);
    if (!record) {
        json_object_put(root);
        struct json_object *not_found = json_object_new_object();
        json_object_object_add(not_found, "status", json_object_new_string("not_found"));
        return respond_with_json_object(resp, not_found, 404);
    }
    json_object_put(root);

    struct json_object *response = json_object_new_object();
    json_object_object_add(response, "status", json_object_new_string("accepted"));
    const char *hash = blockchain_get_last_hash(routes_blockchain);
    json_object_object_add(response, "blockId", json_object_new_string(hash ? hash : ""));
    json_object_object_add(response, "position",
                           json_object_new_int64((int64_t)routes_blockchain->block_count));
    json_object_object_add(response, "programId", json_object_new_string(program_id));

    routes_metrics.chain_submissions++;

    return respond_with_json_object(resp, response, 200);

    return handle_fkv_get(path, resp);
}

static int route_handle_dialog(const kolibri_config_t *cfg,
                               const char *path,
                               const char *body,
                               size_t body_len,
                               http_response_t *resp) {
    (void)path;
    return handle_dialog(cfg, body, body_len, resp);
}

static int route_handle_vm_run(const kolibri_config_t *cfg,
                               const char *path,
                               const char *body,
                               size_t body_len,
                               http_response_t *resp) {
    (void)path;
    return handle_vm_run(cfg, body, body_len, resp);
}

static int route_handle_program_submit(const kolibri_config_t *cfg,
                                       const char *path,
                                       const char *body,
                                       size_t body_len,
                                       http_response_t *resp) {
    (void)path;
    (void)body_len;
    return handle_program_submit(cfg, body, resp);
}

static int route_handle_chain_submit(const kolibri_config_t *cfg,
                                     const char *path,
                                     const char *body,
                                     size_t body_len,
                                     http_response_t *resp) {
    (void)cfg;
    (void)path;
    (void)body_len;
    return handle_chain_submit(body, resp);
}

typedef struct {
    const char *method;
    const char *path;
    int prefix;
    route_handler_fn handler;
} http_route_entry_t;

static const http_route_entry_t HTTP_ROUTES[] = {
    {"GET", "/api/v1/health", 0, route_handle_health},
    {"GET", "/api/v1/metrics", 0, route_handle_metrics},
    {"GET", "/api/v1/fkv/get", 1, route_handle_fkv_get},
    {"POST", "/api/v1/dialog", 0, route_handle_dialog},
    {"POST", "/api/v1/vm/run", 0, route_handle_vm_run},
    {"POST", "/api/v1/program/submit", 0, route_handle_program_submit},
    {"POST", "/api/v1/chain/submit", 0, route_handle_chain_submit},
};

static int route_path_matches(const http_route_entry_t *route, const char *path) {
    if (!route || !path) {
        return 0;
    }
    if (!route->prefix) {
        return strcmp(path, route->path) == 0;
    }
    size_t len = strlen(route->path);
    if (strncmp(path, route->path, len) != 0) {
        return 0;
    }
    char tail = path[len];
    return tail == '\0' || tail == '?' || tail == '/';

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

    routes_metrics.total_requests++;
    if (routes_start_time == 0) {
        routes_start_time = now_ms();
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

    if (strcmp(method, "GET") == 0 && strncmp(path, "/api/v1/fkv/get", strlen("/api/v1/fkv/get")) == 0) {
        return handle_fkv_get(path, resp);
    }


    const char *not_found = "{\"error\":\"not_found\"}";
    return respond_json(resp, not_found, 404);


    for (size_t i = 0; i < sizeof(HTTP_ROUTES) / sizeof(HTTP_ROUTES[0]); ++i) {
        const http_route_entry_t *route = &HTTP_ROUTES[i];
        if (strcmp(method, route->method) != 0) {
            continue;
        }
        if (!route_path_matches(route, path)) {
            continue;
        }
        return route->handler(cfg, path, body, body_len, resp);
    }

    return respond_json(resp, "{\"error\":\"not_found\"}", 404);

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
    }
    routes_blockchain = chain;
    if (!chain) {
        routes_metrics.last_block_time = 0;
    } else {
        update_metrics_after_block();
    }
}

void http_routes_set_ai(KolibriAI *ai) {
    routes_ai = ai;
}

void http_routes_set_ai(KolibriAI *ai) {
    routes_ai = ai;
}
