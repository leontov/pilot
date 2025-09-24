#define _POSIX_C_SOURCE 200809L

#include "http/http_routes.h"

#include <ctype.h>


#include "blockchain.h"
#include "http/http_routes.h"
#include "fkv/fkv.h"
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

static const char *memmem_const(const char *haystack, size_t haystack_len, const char *needle, size_t needle_len) {
    if (!haystack || !needle || needle_len == 0 || haystack_len < needle_len) {
        return NULL;
    }
    for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;


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

                         size_t body_len,
                         http_response_t *resp) {
    (void)body_len;
    if (!cfg || !body || !resp) {
        return -1;
    }
    uint8_t *program = NULL;
    size_t program_len = 0;
    if (parse_program_array(body, &program, &program_len) != 0) {
        return respond_json(resp, "{\"error\":\"invalid_program\"}", 400);
    }
    vm_limits_t limits = {.max_steps = cfg->vm.max_steps, .max_stack = cfg->vm.max_stack};
    uint64_t override = 0;
    if (parse_json_uint_field(body, "max_steps", &override) == 0) {
        limits.max_steps = (uint32_t)override;
    }
    if (parse_json_uint_field(body, "max_stack", &override) == 0) {
        limits.max_stack = (uint32_t)override;
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
    if (rc != 0) {
        return respond_json(resp, "{\"error\":\"vm_failure\"}", 500);
    }
    char buffer[256];
    snprintf(buffer,
             sizeof(buffer),
             "{\"status\":%d,\"result\":%llu,\"steps\":%u,\"halted\":%s}",
             result.status,
             (unsigned long long)result.result,
             result.steps,
             result.halted ? "true" : "false");
    return respond_json(resp, buffer, 200);
}

static char hex_from_digit(uint8_t digit) {
    return (char)((digit < 10) ? ('0' + digit) : ('a' + (digit - 10)));
}

static int handle_fkv_get(const char *path, http_response_t *resp) {
    if (!path || !resp) {

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

        return respond_json(resp, "{\"error\":\"missing_query\"}", 400);
    }
    query++;
    const char *prefix_param = strstr(query, "prefix=");
    if (!prefix_param) {
        return respond_json(resp, "{\"error\":\"missing_prefix\"}", 400);
    }
    prefix_param += strlen("prefix=");
    char prefix[64];
    size_t prefix_len = 0;
    while (prefix_len + 1 < sizeof(prefix) && prefix_param[prefix_len] &&
           prefix_param[prefix_len] != '&') {
        prefix[prefix_len] = prefix_param[prefix_len];
        prefix_len++;
    }
    prefix[prefix_len] = '\0';
    if (prefix_len == 0) {
        return respond_json(resp, "{\"error\":\"invalid_prefix\"}", 400);
    }
    size_t limit = 8;
    const char *limit_param = strstr(query, "limit=");
    if (limit_param) {
        limit_param += strlen("limit=");
        limit = (size_t)strtoul(limit_param, NULL, 10);

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

    uint8_t digits[64];
    size_t digits_len = 0;
    for (size_t i = 0; i < prefix_len; ++i) {
        if (!isdigit((unsigned char)prefix[i])) {
            return respond_json(resp, "{\"error\":\"invalid_prefix\"}", 400);
        }
        digits[digits_len++] = (uint8_t)(prefix[i] - '0');
    }
    fkv_iter_t it = {0};
    if (fkv_get_prefix(digits, digits_len, &it, limit) != 0) {
        return respond_json(resp, "{\"error\":\"fkv_failure\"}", 500);
    }
    char *json = NULL;
    size_t len = 0;
    size_t cap = 0;
    append_char(&json, &len, &cap, '{');
    append_format(&json, &len, &cap, "\"prefix\":\"%s\",\"entries\":[", prefix);
    for (size_t i = 0; i < it.count; ++i) {
        if (i > 0) {
            append_char(&json, &len, &cap, ',');
        }
        char key_buf[128];
        char val_buf[128];
        size_t key_written = 0;
        size_t val_written = 0;
        for (size_t j = 0; j < it.entries[i].key_len && key_written + 1 < sizeof(key_buf); ++j) {
            key_buf[key_written++] = (char)('0' + it.entries[i].key[j]);
        }
        key_buf[key_written] = '\0';
        for (size_t j = 0; j < it.entries[i].value_len && val_written + 1 < sizeof(val_buf); ++j) {
            uint8_t digit = it.entries[i].value[j];
            if (digit <= 9) {
                val_buf[val_written++] = (char)('0' + digit);
            } else {
                val_buf[val_written++] = hex_from_digit(digit);
            }
        }
        val_buf[val_written] = '\0';
        append_format(&json,
                      &len,
                      &cap,
                      "{\"key\":\"%s\",\"value\":\"%s\",\"type\":%d}",
                      key_buf,
                      val_buf,
                      (int)it.entries[i].type);
    }
    append_char(&json, &len, &cap, ']');
    append_char(&json, &len, &cap, '}');
    fkv_iter_free(&it);
    if (!json) {
        return respond_json(resp, "{\"error\":\"oom\"}", 500);
    }
    int rc = respond_json(resp, json, 200);
    free(json);
    return rc;


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
    if (!method || !path || !resp) {
        return -1;
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/health") == 0) {
        return handle_health(resp);
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/metrics") == 0) {
        return handle_metrics(resp);
    }
    if (strncmp(path, "/api/v1/fkv/get", strlen("/api/v1/fkv/get")) == 0 && strcmp(method, "GET") == 0) {
        return handle_fkv_get(path, resp);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/dialog") == 0) {
        return handle_dialog(cfg, body, body_len, resp);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/vm/run") == 0) {
        return handle_vm_run(cfg, body, body_len, resp);
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
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/program/submit") == 0) {
        return handle_program_submit(body, body_len, resp);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/chain/submit") == 0) {
        return handle_chain_submit(body, body_len, resp);
    }
    const char *not_found = "{\"error\":\"not_found\"}";
    return respond_json(resp, not_found, 404);

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
        next_program_id = 1;
    }
    routes_blockchain = chain;
}

void http_routes_set_ai(KolibriAI *ai) {
    routes_ai = ai;
}
