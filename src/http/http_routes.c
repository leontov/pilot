#define _POSIX_C_SOURCE 200809L

#include "http/http_routes.h"

#include "fkv/fkv.h"
#include "kolibri_ai.h"
#include "synthesis/formula_vm_eval.h"
#include "vm/vm.h"

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t routes_start_time = 0;
static Blockchain *routes_blockchain = NULL;
static KolibriAI *routes_ai = NULL;
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
    routes_blockchain = chain;
}

void http_routes_set_ai(KolibriAI *ai) {
    routes_ai = ai;
}
