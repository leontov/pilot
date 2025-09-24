#define _POSIX_C_SOURCE 200809L
#include "http/http_routes.h"

#include "blockchain.h"
#include "fkv/fkv.h"
#include "kolibri_ai.h"
#include "synthesis/formula_vm_eval.h"
#include "vm/vm.h"
#include "formula.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char id[64];
    double poe;
    double mdl;
    double score;
    uint64_t timestamp_ms;
    int committed;
} submitted_program_t;

static uint64_t routes_start_time = 0;
static Blockchain *routes_blockchain = NULL;
static KolibriAI *routes_ai = NULL;

static submitted_program_t *submitted_programs = NULL;
static size_t submitted_count = 0;
static size_t submitted_capacity = 0;
static uint64_t submitted_counter = 0;

static uint64_t total_requests = 0;
static uint64_t total_errors = 0;

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} json_builder_t;

static void json_builder_reset(json_builder_t *builder) {
    if (!builder) {
        return;
    }
    free(builder->data);
    builder->data = NULL;
    builder->len = 0;
    builder->cap = 0;
}

static int json_builder_reserve(json_builder_t *builder, size_t extra) {
    if (!builder) {
        return -1;
    }
    size_t needed = builder->len + extra + 1;
    if (needed <= builder->cap) {
        return 0;
    }
    size_t new_cap = builder->cap ? builder->cap : 128;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    char *tmp = realloc(builder->data, new_cap);
    if (!tmp) {
        return -1;
    }
    builder->data = tmp;
    builder->cap = new_cap;
    return 0;
}

static int json_builder_appendf(json_builder_t *builder, const char *fmt, ...) {
    if (!builder || !fmt) {
        return -1;
    }
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return -1;
    }
    if (json_builder_reserve(builder, (size_t)needed) != 0) {
        va_end(args);
        return -1;
    }
    int written = vsnprintf(builder->data + builder->len, builder->cap - builder->len, fmt, args);
    va_end(args);
    if (written < 0) {
        return -1;
    }
    builder->len += (size_t)written;
    return 0;
}

static int respond_with_builder(http_response_t *resp, json_builder_t *builder, int status) {
    if (!resp || !builder) {
        return -1;
    }
    if (json_builder_reserve(builder, 0) != 0) {
        return -1;
    }
    builder->data[builder->len] = '\0';
    resp->data = builder->data;
    resp->len = builder->len;
    resp->status = status;
    snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
    builder->data = NULL;
    builder->len = 0;
    builder->cap = 0;
    return 0;
}

static int respond_json(http_response_t *resp, const char *json, int status) {
    if (!resp || !json) {
        return -1;
    }
    size_t len = strlen(json);
    char *copy = malloc(len + 1);
    if (!copy) {
        return -1;
    }
    memcpy(copy, json, len + 1);
    resp->data = copy;
    resp->len = len;
    resp->status = status;
    snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
    return 0;
}

static int ensure_submitted_capacity(size_t needed) {
    if (submitted_capacity >= needed) {
        return 0;
    }
    size_t new_capacity = submitted_capacity ? submitted_capacity : 8;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    submitted_program_t *tmp = realloc(submitted_programs, new_capacity * sizeof(*tmp));
    if (!tmp) {
        return -1;
    }
    for (size_t i = submitted_capacity; i < new_capacity; ++i) {
        memset(&tmp[i], 0, sizeof(tmp[i]));
    }
    submitted_programs = tmp;
    submitted_capacity = new_capacity;
    return 0;
}

static submitted_program_t *find_program(const char *id) {
    if (!id) {
        return NULL;
    }
    for (size_t i = 0; i < submitted_count; ++i) {
        if (strcmp(submitted_programs[i].id, id) == 0) {
            return &submitted_programs[i];
        }
    }
    return NULL;
}

static int parse_json_string(const char *json, const char *key, char *buffer, size_t buf_size) {
    if (!json || !key || !buffer || buf_size == 0) {
        return -1;
    }
    const char *key_pos = strstr(json, key);
    if (!key_pos) {
        return -1;
    }
    const char *colon = strchr(key_pos, ':');
    if (!colon) {
        return -1;
    }
    const char *start = colon + 1;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (*start != '"') {
        return -1;
    }
    start++;
    size_t len = 0;
    while (*start && len + 1 < buf_size) {
        if (*start == '\\') {
            start++;
            if (!*start) {
                break;
            }
            buffer[len++] = *start++;
            continue;
        }
        if (*start == '"') {
            buffer[len] = '\0';
            return 0;
        }
        buffer[len++] = *start++;
    }
    return -1;
}

static int parse_json_uint(const char *json, const char *key, uint32_t *out_value) {
    if (!json || !key || !out_value) {
        return -1;
    }
    const char *key_pos = strstr(json, key);
    if (!key_pos) {
        return -1;
    }
    const char *colon = strchr(key_pos, ':');
    if (!colon) {
        return -1;
    }
    const char *start = colon + 1;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (!*start) {
        return -1;
    }
    char *endptr = NULL;
    unsigned long value = strtoul(start, &endptr, 10);
    if (start == endptr) {
        return -1;
    }
    if (value > UINT32_MAX) {
        value = UINT32_MAX;
    }
    *out_value = (uint32_t)value;
    return 0;
}

static int parse_json_int_array(const char *json,
                                const char *key,
                                int32_t **out_values,
                                size_t *out_len) {
    if (!json || !key || !out_values || !out_len) {
        return -1;
    }
    const char *key_pos = strstr(json, key);
    if (!key_pos) {
        return -1;
    }
    const char *bracket = strchr(key_pos, '[');
    if (!bracket) {
        return -1;
    }
    bracket++;
    size_t capacity = 0;
    size_t count = 0;
    int32_t *values = NULL;
    while (*bracket && *bracket != ']') {
        while (*bracket && isspace((unsigned char)*bracket)) {
            bracket++;
        }
        if (*bracket == ']') {
            break;
        }
        char *endptr = NULL;
        long value = strtol(bracket, &endptr, 10);
        if (endptr == bracket) {
            free(values);
            return -1;
        }
        if (*endptr && *endptr != ',' && *endptr != ']') {
            free(values);
            return -1;
        }
        if (count >= capacity) {
            size_t new_capacity = capacity ? capacity * 2 : 8;
            int32_t *tmp = realloc(values, new_capacity * sizeof(*tmp));
            if (!tmp) {
                free(values);
                return -1;
            }
            values = tmp;
            capacity = new_capacity;
        }
        values[count++] = (int32_t)value;
        bracket = endptr;
        if (*bracket == ',') {
            bracket++;
        }
    }
    if (*bracket != ']') {
        free(values);
        return -1;
    }
    *out_values = values;
    *out_len = count;
    return 0;
}

static double compute_program_poe(const vm_result_t *result, size_t program_len) {
    if (!result || result->status != VM_OK) {
        return 0.0;
    }
    double steps_penalty = 1.0 / (1.0 + (double)result->steps / 16.0);
    double magnitude = log1p((double)result->result);
    double magnitude_norm = magnitude / (magnitude + 4.0);
    double poe = steps_penalty * magnitude_norm;
    if (poe > 1.0) {
        poe = 1.0;
    }
    if (poe < 0.0) {
        poe = 0.0;
    }
    if (program_len > 0) {
        double brevity_bonus = 1.0 / (1.0 + (double)program_len / 32.0);
        poe = fmin(1.0, poe * 0.7 + brevity_bonus * 0.3);
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
    return mdl;
}

static void record_program_metrics(submitted_program_t *program,
                                   const vm_result_t *result,
                                   size_t program_len) {
    if (!program || !result) {
        return;
    }
    program->poe = compute_program_poe(result, program_len);
    program->mdl = compute_program_mdl(program_len);
    program->score = program->poe - 0.15 * program->mdl;
    if (program->score < 0.0) {
        program->score = 0.0;
    }
    program->timestamp_ms = now_ms();
}

static int build_trace_json(const vm_trace_t *trace, json_builder_t *builder) {
    if (!builder) {
        return -1;
    }
    if (!trace || trace->count == 0) {
        return json_builder_appendf(builder, "[]");
    }
    if (json_builder_appendf(builder, "[") != 0) {
        return -1;
    }
    for (size_t i = 0; i < trace->count; ++i) {
        const vm_trace_entry_t *entry = &trace->entries[i];
        if (json_builder_appendf(builder,
                                 "{\"step\":%u,\"ip\":%u,\"opcode\":%u,\"stack_top\":%lld,\"gas_left\":%u}",
                                 entry->step,
                                 entry->ip,
                                 entry->opcode,
                                 (long long)entry->stack_top,
                                 entry->gas_left) != 0) {
            return -1;
        }
        if (i + 1 < trace->count) {
            if (json_builder_appendf(builder, ",") != 0) {
                return -1;
            }
        }
    }
    return json_builder_appendf(builder, "]");
}

static int handle_health(http_response_t *resp) {
    uint64_t uptime_ms = routes_start_time ? (now_ms() - routes_start_time) : 0;
    size_t blocks = routes_blockchain ? routes_blockchain->block_count : 0;
    json_builder_t builder = {0};
    if (json_builder_appendf(&builder,
                             "{\"uptime_ms\":%llu,\"blockchain_attached\":%s,\"blocks\":%zu}",
                             (unsigned long long)uptime_ms,
                             routes_blockchain ? "true" : "false",
                             blocks) != 0) {
        json_builder_reset(&builder);
        return -1;
    }
    return respond_with_builder(resp, &builder, 200);
}

static int handle_metrics(http_response_t *resp) {
    json_builder_t builder = {0};
    if (json_builder_appendf(&builder,
                             "{\"requests\":%llu,\"errors\":%llu,\"programs\":%zu}",
                             (unsigned long long)total_requests,
                             (unsigned long long)total_errors,
                             submitted_count) != 0) {
        json_builder_reset(&builder);
        return -1;
    }
    return respond_with_builder(resp, &builder, 200);
}

static int evaluate_expression(const kolibri_config_t *cfg,
                               const char *expression,
                               vm_result_t *out_result,
                               uint32_t *out_steps) {
    if (!expression) {
        return -1;
    }
    uint8_t *bytecode = NULL;
    size_t bytecode_len = 0;
    if (formula_vm_compile_from_text(expression, &bytecode, &bytecode_len) != 0) {
        return -1;
    }
    vm_limits_t limits = {0};
    if (cfg) {
        limits.max_steps = cfg->vm.max_steps ? cfg->vm.max_steps : 256u;
        limits.max_stack = cfg->vm.max_stack ? cfg->vm.max_stack : 64u;
    } else {
        limits.max_steps = 256u;
        limits.max_stack = 64u;
    }
    prog_t prog = {.code = bytecode, .len = bytecode_len};
    vm_result_t result = {0};
    int rc = vm_run(&prog, &limits, NULL, &result);
    free(bytecode);
    if (rc != 0 || result.status != VM_OK) {
        return -1;
    }
    if (out_result) {
        *out_result = result;
    }
    if (out_steps) {
        *out_steps = result.steps;
    }
    return 0;
}

static int handle_dialog(const kolibri_config_t *cfg,
                         const char *body,
                         size_t body_len,
                         http_response_t *resp) {
    (void)body_len;
    char input[256] = {0};
    if (body && parse_json_string(body, "\"input\"", input, sizeof(input)) == 0) {
        vm_result_t result = {0};
        uint32_t steps = 0;
        if (evaluate_expression(cfg, input, &result, &steps) == 0) {
            json_builder_t builder = {0};
            if (json_builder_appendf(&builder,
                                     "{\"answer\":\"%llu\",\"result\":%llu,\"steps\":%u}",
                                     (unsigned long long)result.result,
                                     (unsigned long long)result.result,
                                     steps) != 0) {
                json_builder_reset(&builder);
                return -1;
            }
            return respond_with_builder(resp, &builder, 200);
        }
    }
    const char *fallback = "{\"answer\":\"Kolibri is online\"}";
    return respond_json(resp, fallback, 200);
}

static int handle_vm_run(const kolibri_config_t *cfg,
                         const char *body,
                         size_t body_len,
                         http_response_t *resp) {
    (void)body_len;
    int32_t *values = NULL;
    size_t value_count = 0;
    if (!body || parse_json_int_array(body, "\"program\"", &values, &value_count) != 0 || value_count == 0) {
        free(values);
        return respond_json(resp, "{\"error\":\"invalid_program\"}", 400);
    }
    uint32_t gas_limit = 0;
    if (parse_json_uint(body, "\"gasLimit\"", &gas_limit) != 0) {
        gas_limit = 0;
    }
    uint8_t *bytecode = calloc(value_count, sizeof(uint8_t));
    if (!bytecode) {
        free(values);
        return respond_json(resp, "{\"error\":\"oom\"}", 500);
    }
    for (size_t i = 0; i < value_count; ++i) {
        int32_t v = values[i];
        if (v < 0 || v > 255) {
            free(values);
            free(bytecode);
            return respond_json(resp, "{\"error\":\"invalid_byte\"}", 400);
        }
        bytecode[i] = (uint8_t)v;
    }
    free(values);

    vm_limits_t limits = {0};
    if (cfg) {
        limits.max_steps = cfg->vm.max_steps ? cfg->vm.max_steps : 256u;
        limits.max_stack = cfg->vm.max_stack ? cfg->vm.max_stack : 64u;
    } else {
        limits.max_steps = 256u;
        limits.max_stack = 64u;
    }
    if (gas_limit > 0) {
        limits.max_steps = gas_limit;
    }

    vm_trace_t trace = {0};
    vm_result_t result = {0};
    uint32_t trace_capacity = cfg && cfg->vm.trace_depth ? cfg->vm.trace_depth : 16u;
    if (trace_capacity > 0) {
        trace.entries = calloc(trace_capacity, sizeof(vm_trace_entry_t));
        if (!trace.entries) {
            free(bytecode);
            return respond_json(resp, "{\"error\":\"oom\"}", 500);
        }
        trace.capacity = trace_capacity;
    }

    prog_t prog = {.code = bytecode, .len = value_count};
    int rc = vm_run(&prog, &limits, trace.entries ? &trace : NULL, &result);
    free(bytecode);

    if (rc != 0) {
        free(trace.entries);
        return respond_json(resp, "{\"error\":\"vm_failure\"}", 500);
    }

    json_builder_t builder = {0};
    if (json_builder_appendf(&builder,
                             "{\"status\":\"%s\",\"result\":%llu,\"steps\":%u,\"gasUsed\":%u,\"trace\":",
                             result.status == VM_OK ? "ok" : "error",
                             (unsigned long long)result.result,
                             result.steps,
                             result.steps) != 0) {
        free(trace.entries);
        json_builder_reset(&builder);
        return -1;
    }
    if (build_trace_json(&trace, &builder) != 0) {
        free(trace.entries);
        json_builder_reset(&builder);
        return -1;
    }
    free(trace.entries);
    if (json_builder_appendf(&builder, "}") != 0) {
        json_builder_reset(&builder);
        return -1;
    }
    return respond_with_builder(resp, &builder, 200);
}

static void format_int_array(const int32_t *values, size_t count, char *buffer, size_t buffer_len) {
    if (!values || !buffer || buffer_len == 0) {
        return;
    }
    size_t offset = 0;
    for (size_t i = 0; i < count && offset + 4 < buffer_len; ++i) {
        int written = snprintf(buffer + offset, buffer_len - offset, "%s%ld", i == 0 ? "" : ",", (long)values[i]);
        if (written < 0) {
            break;
        }
        offset += (size_t)written;
    }
    if (offset >= buffer_len) {
        buffer[buffer_len - 1] = '\0';
    }
}

static int handle_program_submit(const kolibri_config_t *cfg,
                                 const char *body,
                                 size_t body_len,
                                 http_response_t *resp) {
    (void)body_len;
    int32_t *values = NULL;
    size_t value_count = 0;
    if (!body || parse_json_int_array(body, "\"bytecode\"", &values, &value_count) != 0 || value_count == 0) {
        free(values);
        return respond_json(resp, "{\"error\":\"invalid_program\"}", 400);
    }
    if (ensure_submitted_capacity(submitted_count + 1) != 0) {
        free(values);
        return respond_json(resp, "{\"error\":\"oom\"}", 500);
    }
    submitted_program_t *record = &submitted_programs[submitted_count];
    memset(record, 0, sizeof(*record));
    submitted_counter++;
    snprintf(record->id, sizeof(record->id), "program-%llu", (unsigned long long)submitted_counter);

    char bytecode_string[512];
    format_int_array(values, value_count, bytecode_string, sizeof(bytecode_string));

    uint8_t *bytecode = calloc(value_count, sizeof(uint8_t));
    if (!bytecode) {
        free(values);
        return respond_json(resp, "{\"error\":\"oom\"}", 500);
    }
    for (size_t i = 0; i < value_count; ++i) {
        int32_t v = values[i];
        if (v < 0 || v > 255) {
            free(values);
            free(bytecode);
            return respond_json(resp, "{\"error\":\"invalid_byte\"}", 400);
        }
        bytecode[i] = (uint8_t)v;
    }

    vm_limits_t limits = {0};
    if (cfg) {
        limits.max_steps = cfg->vm.max_steps ? cfg->vm.max_steps : 256u;
        limits.max_stack = cfg->vm.max_stack ? cfg->vm.max_stack : 64u;
    } else {
        limits.max_steps = 256u;
        limits.max_stack = 64u;
    }

    prog_t prog = {.code = bytecode, .len = value_count};
    vm_result_t result = {0};
    int rc = vm_run(&prog, &limits, NULL, &result);
    free(bytecode);
    if (rc != 0) {
        free(values);
        return respond_json(resp, "{\"error\":\"vm_failure\"}", 500);
    }

    record_program_metrics(record, &result, value_count);

    if (routes_blockchain) {
        Formula formula = {0};
        snprintf(formula.id, sizeof(formula.id), "%s", record->id);
        formula.representation = FORMULA_REPRESENTATION_TEXT;
        formula.effectiveness = record->poe;
        formula.created_at = time(NULL);
        formula.tests_passed = (result.status == VM_OK) ? 1u : 0u;
        formula.confirmations = 0;
        formula.type = FORMULA_LINEAR;
        snprintf(formula.content, sizeof(formula.content), "%s", bytecode_string);
        Formula *formulas[1] = {&formula};
        (void)blockchain_add_block(routes_blockchain, formulas, 1);
    }

    free(values);

    json_builder_t builder = {0};
    if (json_builder_appendf(&builder,
                             "{\"programId\":\"%s\",\"poe\":%.6f,\"mdl\":%.6f,\"score\":%.6f,\"result\":%llu,"
                             "\"PoE\":%.6f,\"MDL\":%.6f}",
                             record->id,
                             record->poe,
                             record->mdl,
                             record->score,
                             (unsigned long long)result.result,
                             record->poe,
                             record->mdl) != 0) {
        json_builder_reset(&builder);
        return -1;
    }
    submitted_count++;
    return respond_with_builder(resp, &builder, 200);
}

static int handle_chain_submit(const char *body, size_t body_len, http_response_t *resp) {
    (void)body_len;
    if (!body) {
        return respond_json(resp, "{\"status\":\"bad_request\"}", 400);
    }
    char program_id[64];
    if (parse_json_string(body, "\"program_id\"", program_id, sizeof(program_id)) != 0 &&
        parse_json_string(body, "\"programId\"", program_id, sizeof(program_id)) != 0) {
        return respond_json(resp, "{\"status\":\"bad_request\"}", 400);
    }
    submitted_program_t *record = find_program(program_id);
    if (!record) {
        return respond_json(resp, "{\"status\":\"not_found\"}", 404);
    }
    record->committed = 1;
    json_builder_t builder = {0};
    size_t position = routes_blockchain ? routes_blockchain->block_count : 0;
    if (json_builder_appendf(&builder,
                             "{\"status\":\"accepted\",\"programId\":\"%s\",\"position\":%zu}",
                             record->id,
                             position) != 0) {
        json_builder_reset(&builder);
        return -1;
    }
    return respond_with_builder(resp, &builder, 200);
}

static int handle_fkv_get(const char *path, http_response_t *resp) {
    if (!path) {
        return respond_json(resp, "{\"error\":\"bad_request\"}", 400);
    }
    const char *query = strchr(path, '?');
    if (!query) {
        return respond_json(resp, "{\"error\":\"bad_request\"}", 400);
    }
    query++;
    const char *prefix_key = "prefix=";
    const char *prefix_start = strstr(query, prefix_key);
    if (!prefix_start) {
        return respond_json(resp, "{\"error\":\"bad_request\"}", 400);
    }
    prefix_start += strlen(prefix_key);
    uint8_t digits[64];
    size_t digit_count = 0;
    while (prefix_start[digit_count] && prefix_start[digit_count] != '&' && digit_count < sizeof(digits)) {
        char ch = prefix_start[digit_count];
        if (ch < '0' || ch > '9') {
            break;
        }
        digits[digit_count] = (uint8_t)(ch - '0');
        digit_count++;
    }
    if (digit_count == 0) {
        return respond_json(resp, "{\"values\":[]}", 200);
    }
    fkv_iter_t iter = {0};
    if (fkv_get_prefix(digits, digit_count, &iter, 16) != 0) {
        return respond_json(resp, "{\"error\":\"fkv_failure\"}", 500);
    }
    json_builder_t builder = {0};
    if (json_builder_appendf(&builder, "{\"values\":[") != 0) {
        fkv_iter_free(&iter);
        json_builder_reset(&builder);
        return -1;
    }
    for (size_t i = 0; i < iter.count; ++i) {
        const fkv_entry_t *entry = &iter.entries[i];
        if (json_builder_appendf(&builder, "{\"key\":\"") != 0) {
            fkv_iter_free(&iter);
            json_builder_reset(&builder);
            return -1;
        }
        for (size_t j = 0; j < entry->key_len; ++j) {
            if (json_builder_appendf(&builder, "%u", entry->key[j]) != 0) {
                fkv_iter_free(&iter);
                json_builder_reset(&builder);
                return -1;
            }
        }
        if (json_builder_appendf(&builder, "\",\"value\":\"") != 0) {
            fkv_iter_free(&iter);
            json_builder_reset(&builder);
            return -1;
        }
        for (size_t j = 0; j < entry->value_len; ++j) {
            uint8_t v = entry->value[j];
            if (v <= 9) {
                if (json_builder_appendf(&builder, "%u", v) != 0) {
                    fkv_iter_free(&iter);
                    json_builder_reset(&builder);
                    return -1;
                }
            } else {
                if (json_builder_appendf(&builder, "%c", (char)v) != 0) {
                    fkv_iter_free(&iter);
                    json_builder_reset(&builder);
                    return -1;
                }
            }
        }
        if (json_builder_appendf(&builder, "\"}") != 0) {
            fkv_iter_free(&iter);
            json_builder_reset(&builder);
            return -1;
        }
        if (i + 1 < iter.count) {
            if (json_builder_appendf(&builder, ",") != 0) {
                fkv_iter_free(&iter);
                json_builder_reset(&builder);
                return -1;
            }
        }
    }
    fkv_iter_free(&iter);
    if (json_builder_appendf(&builder, "]}") != 0) {
        json_builder_reset(&builder);
        return -1;
    }
    return respond_with_builder(resp, &builder, 200);
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
    total_requests++;
    int rc = 0;
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/health") == 0) {
        rc = handle_health(resp);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/metrics") == 0) {
        rc = handle_metrics(resp);
    } else if (strcmp(method, "GET") == 0 && strncmp(path, "/api/v1/fkv/get", strlen("/api/v1/fkv/get")) == 0) {
        rc = handle_fkv_get(path, resp);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/dialog") == 0) {
        rc = handle_dialog(cfg, body, body_len, resp);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/vm/run") == 0) {
        rc = handle_vm_run(cfg, body, body_len, resp);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/program/submit") == 0) {
        rc = handle_program_submit(cfg, body, body_len, resp);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/chain/submit") == 0) {
        rc = handle_chain_submit(body, body_len, resp);
    } else {
        rc = respond_json(resp, "{\"error\":\"not_found\"}", 404);
    }
    if (rc != 0 || (resp && resp->status >= 400)) {
        total_errors++;
    }
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
    (void)routes_ai;
}
