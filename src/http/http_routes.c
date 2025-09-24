


/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */
#define _POSIX_C_SOURCE 200809L
#include "http/http_routes.h"

#include "blockchain.h"
#include "fkv/fkv.h"
#include "formula.h"
#include "vm/vm.h"

#include <json-c/json.h>

#include <ctype.h>
#include <math.h>
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
}

static int handle_dialog(const char *body, size_t body_len, http_response_t *resp) {
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
    if (!chain) {
        routes_metrics.last_block_time = 0;
    } else {
        update_metrics_after_block();
    }
}
