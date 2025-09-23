#include "http/http_routes.h"

#include "blockchain.h"
#include "fkv/fkv.h"
#include "kolibri_ai.h"
#include "util/log.h"
#include "vm/vm.h"

#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <json-c/json.h>

#define WEB_DIST_DIR "web/dist"

static uint64_t server_start_ms = 0;


void http_routes_set_start_time(uint64_t ms_since_epoch) {
    server_start_ms = ms_since_epoch;
}


}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
}

static void set_response(http_response_t *resp, int status, const char *content_type, const char *body) {
    size_t len = strlen(body);
    resp->data = malloc(len + 1);
    if (!resp->data) {
        resp->status = 500;
        snprintf(resp->content_type, sizeof(resp->content_type), "text/plain");
        resp->data = NULL;
        resp->len = 0;
        return;
    }
    memcpy(resp->data, body, len + 1);
    resp->len = len;
    resp->status = status;
    snprintf(resp->content_type, sizeof(resp->content_type), "%s", content_type);
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

static int ensure_capacity(uint8_t **buf, size_t *cap, size_t needed) {
    if (*cap >= needed) {
        return 0;
    }
    size_t new_cap = (*cap == 0) ? needed : *cap;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    uint8_t *tmp = realloc(*buf, new_cap);
    if (!tmp) {
        return -1;
    }
    *buf = tmp;
    *cap = new_cap;
    return 0;
}

enum parse_error {
    PARSE_OK = 0,
    PARSE_ERR_JSON = -1,
    PARSE_ERR_OOM = -2,
    PARSE_ERR_ROOT_TYPE = -3,
    PARSE_ERR_MISSING_FIELD = -4,
    PARSE_ERR_FIELD_TYPE = -5,
    PARSE_ERR_INVALID_VALUE = -6,
};

static int parse_json_object(const char *body, size_t body_len, struct json_object **out) {
    if (!out) {
        return PARSE_ERR_JSON;
    }
    struct json_tokener *tok = json_tokener_new();
    if (!tok) {
        return PARSE_ERR_OOM;
    }
    struct json_object *root = json_tokener_parse_ex(tok, body, (int)body_len);
    enum json_tokener_error jerr = json_tokener_get_error(tok);
    json_tokener_free(tok);
    if (jerr != json_tokener_success || !root) {
        if (root) {
            json_object_put(root);
        }
        return PARSE_ERR_JSON;
    }
    if (!json_object_is_type(root, json_type_object)) {
        json_object_put(root);
        return PARSE_ERR_ROOT_TYPE;
    }
    *out = root;
    return PARSE_OK;
}

static int parse_digits_array(const char *body, size_t body_len, struct json_object **out_root, const char **out_input) {
    struct json_object *root = NULL;
    int rc = parse_json_object(body, body_len, &root);
    if (rc != PARSE_OK) {
        return rc;
    }
    struct json_object *input_obj = NULL;
    if (!json_object_object_get_ex(root, "input", &input_obj)) {
        json_object_put(root);
        return PARSE_ERR_MISSING_FIELD;
    }
    if (!json_object_is_type(input_obj, json_type_string)) {
        json_object_put(root);
        return PARSE_ERR_FIELD_TYPE;
    }
    if (out_root) {
        *out_root = root;
    } else {
        json_object_put(root);
    }
    if (out_input) {
        *out_input = json_object_get_string(input_obj);
    }
    return PARSE_OK;
}

static int build_digits_from_input(const char *input, uint8_t **out, size_t *out_len) {
    if (!input || !out || !out_len) {
        return PARSE_ERR_INVALID_VALUE;
    }
    size_t cap = 0;
    uint8_t *buf = NULL;
    size_t count = 0;
    for (const char *p = input; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (isspace(ch)) {
            continue;
        }
        if (isdigit(ch)) {
            if (ensure_capacity(&buf, &cap, count + 1) != 0) {
                free(buf);
                return PARSE_ERR_OOM;
            }
            buf[count++] = (uint8_t)(ch - '0');
            continue;
        }
        if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
            if (ensure_capacity(&buf, &cap, count + 1) != 0) {
                free(buf);
                return PARSE_ERR_OOM;
            }
            buf[count++] = (uint8_t)ch;
            continue;
        }
        free(buf);
        return PARSE_ERR_INVALID_VALUE;
    }
    if (count == 0) {
        free(buf);
        return PARSE_ERR_INVALID_VALUE;
    }
    *out = buf;
    *out_len = count;
    return PARSE_OK;
}

struct byte_buffer {
    uint8_t *data;
    size_t len;
    size_t cap;
};

static int bb_reserve(struct byte_buffer *bb, size_t extra) {
    if (bb->len + extra <= bb->cap) {
        return 0;
    }
    size_t new_cap = bb->cap ? bb->cap : 32;
    while (new_cap < bb->len + extra) {
        new_cap *= 2;
    }
    uint8_t *tmp = realloc(bb->data, new_cap);
    if (!tmp) {
        return -1;
    }
    bb->data = tmp;
    bb->cap = new_cap;
    return 0;
}

static int bb_push(struct byte_buffer *bb, uint8_t byte) {
    if (bb_reserve(bb, 1) != 0) {
        return -1;
    }
    bb->data[bb->len++] = byte;
    return 0;
}

static int emit_push_number(struct byte_buffer *bb, uint64_t value) {
    if (bb_push(bb, 0x01) != 0 || bb_push(bb, 0x00) != 0) {
        return -1;
    }
    char digits[32];
    snprintf(digits, sizeof(digits), "%llu", (unsigned long long)value);
    for (size_t i = 0; digits[i]; ++i) {
        if (bb_push(bb, 0x01) != 0 || bb_push(bb, 0x02) != 0) {
            return -1;
        }
        if (bb_push(bb, 0x01) != 0 || bb_push(bb, 0x05) != 0) {
            return -1;
        }
        if (bb_push(bb, 0x04) != 0) {
            return -1;
        }
        if (bb_push(bb, 0x04) != 0) {
            return -1;
        }
        uint8_t digit = (uint8_t)(digits[i] - '0');
        if (bb_push(bb, 0x01) != 0 || bb_push(bb, digit) != 0) {
            return -1;
        }
        if (bb_push(bb, 0x02) != 0) {
            return -1;
        }
    }
    return 0;
}

static int compile_expression_program(const uint8_t *digits, size_t len, struct byte_buffer *bb) {
    char expr[128];
    size_t expr_len = 0;
    for (size_t i = 0; i < len && expr_len + 1 < sizeof(expr); ++i) {
        uint8_t v = digits[i];
        if (v <= 9) {
            expr[expr_len++] = (char)('0' + v);
        } else if (v == 42 || v == 43 || v == 45 || v == 47) {
            expr[expr_len++] = (char)v;
        }
    }
    expr[expr_len] = '\0';
    if (expr_len == 0) {
        return -1;
    }
    char *op_ptr = NULL;
    char *ops = "+-*/";
    for (char *p = expr; *p; ++p) {
        if (strchr(ops, *p)) {
            op_ptr = p;
            break;
        }
    }
    if (!op_ptr) {
        return -1;
    }
    char op = *op_ptr;
    *op_ptr = '\0';
    uint64_t lhs = strtoull(expr, NULL, 10);
    uint64_t rhs = strtoull(op_ptr + 1, NULL, 10);

    if (emit_push_number(bb, lhs) != 0) {
        return -1;
    }
    if (emit_push_number(bb, rhs) != 0) {
        return -1;
    }

    switch (op) {
    case '+':
        if (bb_push(bb, 0x02) != 0) {
            return -1;
        }
        break;
    case '-':
        if (bb_push(bb, 0x03) != 0) {
            return -1;
        }
        break;
    case '*':
        if (bb_push(bb, 0x04) != 0) {
            return -1;
        }
        break;
    case '/':
        if (bb_push(bb, 0x05) != 0) {
            return -1;
        }
        break;
    default:
        return -1;
    }

    if (bb_push(bb, 0x0B) != 0) {
        return -1;
    }
    return 0;
}


    }
    if (!json_object_is_type(program_obj, json_type_array)) {
        json_object_put(root);
        return PARSE_ERR_FIELD_TYPE;
    }
    if (out_root) {
        *out_root = root;
    } else {
        json_object_put(root);
    }
    if (out_program) {
        *out_program = program_obj;
    }
    return PARSE_OK;
}

static int build_program_bytes(struct json_object *program_array, uint8_t **out, size_t *out_len) {
    if (!program_array || !out || !out_len) {
        return PARSE_ERR_INVALID_VALUE;
    }
    size_t len = json_object_array_length(program_array);
    uint8_t *buf = NULL;
    if (len > 0) {
        buf = malloc(len);
        if (!buf) {
            return PARSE_ERR_OOM;
        }
    }
    for (size_t i = 0; i < len; ++i) {
        struct json_object *item = json_object_array_get_idx(program_array, (int)i);
        if (!item || !json_object_is_type(item, json_type_int)) {
            free(buf);
            return PARSE_ERR_INVALID_VALUE;
        }
        int v = json_object_get_int(item);
        if (v < 0 || v > 255) {
            free(buf);
            return PARSE_ERR_INVALID_VALUE;
        }
        buf[i] = (uint8_t)v;
    }
    *out = buf;
    *out_len = len;
    return PARSE_OK;
}

static int parse_program_array(const char *body, size_t body_len, uint8_t **out, size_t *out_len) {
    return parse_named_byte_array(body, body_len, "\"program\"", out, out_len);
}

static int parse_bytecode_array(const char *body, size_t body_len, uint8_t **out, size_t *out_len) {
    return parse_named_byte_array(body, body_len, "\"bytecode\"", out, out_len);
}

static int parse_string_field(const char *body, const char *field, char *out, size_t out_size) {
    if (!body || !field || !out || out_size == 0) {
        return -1;
    }
    const char *field_key = strstr(body, field);
    if (!field_key) {
        return -1;
    }
    const char *colon = strchr(field_key, ':');
    if (!colon) {
        return -1;
    }
    const char *start = strchr(colon, '\"');
    if (!start) {
        return -1;
    }
    start++;
    const char *end = strchr(start, '\"');
    if (!end) {
        return -1;
    }
    size_t len = (size_t)(end - start);
    if (len + 1 > out_size) {
        return -1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}

static void compute_program_metrics(const uint8_t *bytecode, size_t len, double *poe, double *mdl, double *score) {
    double local_poe = 0.0;
    double local_mdl = 0.0;
    double local_score = 0.0;

    if (bytecode && len > 0) {
        size_t active = 0;
        size_t sum = 0;
        for (size_t i = 0; i < len; ++i) {
            if (bytecode[i] != 0) {
                active++;
            }
            sum += bytecode[i];
        }
        local_poe = (double)active / (double)len;
        local_mdl = (double)len;
        double normalized = (double)sum / (255.0 * (double)len);
        local_score = local_poe * 0.7 + normalized * 0.3 - 0.01 * local_mdl;
        if (local_score < 0.0) {
            local_score = 0.0;
        }
    }

    if (poe) {
        *poe = local_poe;
    }
    if (mdl) {
        *mdl = local_mdl;
    }
    if (score) {
        *score = local_score;
    }
}

static void format_bytecode_content(char *dest, size_t dest_size, const uint8_t *bytecode, size_t len) {
    if (!dest || dest_size == 0) {
        return;
    }
    dest[0] = '\0';
    size_t offset = 0;
    for (size_t i = 0; i < len && offset + 4 < dest_size; ++i) {
        int written = snprintf(dest + offset, dest_size - offset, "%s%u", (i == 0) ? "" : " ", (unsigned)bytecode[i]);
        if (written <= 0) {
            break;
        }
        if ((size_t)written >= dest_size - offset) {
            dest[dest_size - 1] = '\0';
            break;
        }
        offset += (size_t)written;
    }
}

static const Formula *blockchain_find_formula(const Blockchain *chain, const char *program_id) {
    if (!chain || !program_id) {
        return NULL;
    }
    for (size_t i = 0; i < chain->block_count; ++i) {
        const Block *block = chain->blocks[i];
        if (!block) {
            continue;
        }
        for (size_t j = 0; j < block->formula_count; ++j) {
            const Formula *formula = block->formulas[j];
            if (formula && strcmp(formula->id, program_id) == 0) {
                return formula;
            }
        }
    }
    return NULL;
}

static void respond_program_submit(const kolibri_config_t *cfg, const char *body, size_t body_len, http_response_t *resp) {
    (void)cfg;
    if (!global_blockchain) {
        set_response(resp, 503, "application/json", "{\"error\":\"blockchain unavailable\"}");
        return;
    }

    uint8_t *bytecode = NULL;
    size_t byte_len = 0;
    if (parse_bytecode_array(body, body_len, &bytecode, &byte_len) != 0 || byte_len == 0) {
        free(bytecode);
        set_response(resp, 400, "application/json", "{\"error\":\"invalid bytecode\"}");
        return;
    }

    double poe = 0.0;
    double mdl = 0.0;
    double score = 0.0;
    compute_program_metrics(bytecode, byte_len, &poe, &mdl, &score);

    Formula formula = {0};
    unsigned long current_id = next_program_id;
    snprintf(formula.id, sizeof(formula.id), "program-%lu", current_id);
    formula.representation = FORMULA_REPRESENTATION_TEXT;
    formula.type = FORMULA_COMPOSITE;
    formula.created_at = time(NULL);
    formula.effectiveness = poe;
    format_bytecode_content(formula.content, sizeof(formula.content), bytecode, byte_len);

    Formula *formulas[1];
    formulas[0] = &formula;
    bool added = blockchain_add_block(global_blockchain, formulas, 1);
    free(bytecode);
    if (!added) {
        set_response(resp, 500, "application/json", "{\"error\":\"blockchain append failed\"}");
        return;
    }

    next_program_id = current_id + 1;

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"PoE\":%.3f,\"MDL\":%.3f,\"score\":%.3f}", poe, mdl, score);
    set_response(resp, 200, "application/json", buf);
}

static void respond_chain_submit(const char *body, size_t body_len, http_response_t *resp) {
    (void)body_len;
    if (!global_blockchain) {
        set_response(resp, 503, "application/json", "{\"status\":\"unavailable\"}");
        return;
    }

    char program_id[64];
    if (parse_string_field(body, "\"program_id\"", program_id, sizeof(program_id)) != 0) {
        set_response(resp, 400, "application/json", "{\"status\":\"invalid_request\"}");
        return;
    }

    if (!blockchain_verify(global_blockchain)) {
        set_response(resp, 500, "application/json", "{\"status\":\"invalid_chain\"}");
        return;
    }

    if (!blockchain_find_formula(global_blockchain, program_id)) {
        set_response(resp, 404, "application/json", "{\"status\":\"not_found\"}");
        return;
    }

    set_response(resp, 200, "application/json", "{\"status\":\"accepted\"}");
}

static void append_trace_json(char **out, size_t *out_len, size_t *out_cap, const vm_trace_t *trace) {
    if (!trace || trace->count == 0) {
        return;
    }
    for (size_t i = 0; i < trace->count; ++i) {
        const vm_trace_entry_t *e = &trace->entries[i];
        int needed = snprintf(NULL, 0, "{\"step\":%u,\"ip\":%u,\"op\":%u,\"stack\":%lld,\"gas\":%u}",
                              e->step, e->ip, (unsigned)e->opcode, (long long)e->stack_top, e->gas_left);
        if (*out_len + (size_t)needed + 2 > *out_cap) {
            size_t new_cap = (*out_cap == 0) ? 256 : *out_cap * 2;
            while (new_cap < *out_len + (size_t)needed + 2) {
                new_cap *= 2;
            }
            char *tmp = realloc(*out, new_cap);
            if (!tmp) {
                return;
            }
            *out = tmp;
            *out_cap = new_cap;
        }
        int written = snprintf(*out + *out_len, *out_cap - *out_len, "%s{\"step\":%u,\"ip\":%u,\"op\":%u,\"stack\":%lld,\"gas\":%u}",
                               (i == 0) ? "" : ",",
                               e->step, e->ip, (unsigned)e->opcode, (long long)e->stack_top, e->gas_left);
        if (written > 0) {
            *out_len += (size_t)written;
        }
    }
}

static size_t read_memory_usage_bytes(void) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(__APPLE__) && defined(__MACH__)
        return (size_t)usage.ru_maxrss;
#else
        return (size_t)usage.ru_maxrss * 1024u;
#endif
    }
    FILE *fp = fopen("/proc/self/statm", "r");
    if (!fp) {
        return 0;
    }
    long pages = 0;
    if (fscanf(fp, "%ld", &pages) != 1) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return 0;
    }
    return (size_t)pages * (size_t)page_size;
}

static void respond_health(const kolibri_config_t *cfg, http_response_t *resp) {
    (void)cfg;
    uint64_t now = now_ms();
    uint64_t uptime = server_start_ms ? (now - server_start_ms) : 0;
    size_t memory_bytes = read_memory_usage_bytes();
    char buf[256];
    snprintf(buf,
             sizeof(buf),
             "{\"uptime_ms\":%llu,\"memory_bytes\":%zu,\"peers\":0,\"blocks\":0}",
             (unsigned long long)uptime,
             memory_bytes);
    set_response(resp, 200, "application/json", buf);
}

static void respond_metrics(const kolibri_config_t *cfg, http_response_t *resp) {
    uint64_t now = now_ms();
    uint64_t uptime = server_start_ms ? (now - server_start_ms) : 0;
    size_t memory_bytes = read_memory_usage_bytes();
    char buf[256];
    snprintf(buf,
             sizeof(buf),
             "{\"uptime_ms\":%llu,\"memory_bytes\":%zu,\"peers\":0,\"blocks\":0,"
             "\"vm\":{\"max_steps\":%u,\"max_stack\":%u,\"trace_depth\":%u}}",
             (unsigned long long)uptime,
             memory_bytes,
             cfg->vm.max_steps,
             cfg->vm.max_stack,
             cfg->vm.trace_depth);
    set_response(resp, 200, "application/json", buf);
}


    prog_t prog = {prog_bytes, prog_len};
    vm_limits_t limits = {cfg->vm.max_steps, cfg->vm.max_stack};
    vm_trace_entry_t *entries = calloc(cfg->vm.trace_depth, sizeof(vm_trace_entry_t));
    vm_trace_t trace = {entries, cfg->vm.trace_depth, 0, 0};
    vm_result_t result;
    int rc = vm_run(&prog, &limits, &trace, &result);

    char *json = NULL;
    size_t len = 0;
    size_t cap = 256;
    json = malloc(cap);
    if (!json) {
        set_response(resp, 500, "application/json", "{\"error\":\"oom\"}");
        free(entries);
        return;
    }

                           result.status,
                           result.steps,
                           (unsigned long long)result.result,
                           (unsigned)result.halted);
    if (written < 0) {
        free(json);
        free(entries);
        set_response(resp, 500, "application/json", "{\"error\":\"format\"}");
        return;
    }
    len = (size_t)written;
    append_trace_json(&json, &len, &cap, &trace);
    if (len + 3 > cap) {
        char *tmp = realloc(json, len + 3);
        if (!tmp) {
            free(json);
            free(entries);
            set_response(resp, 500, "application/json", "{\"error\":\"oom\"}");
            return;
        }
        json = tmp;
        cap = len + 3;
    }
    snprintf(json + len, cap - len, "]}");
    len = strlen(json);

    resp->data = json;
    resp->len = len;
    if (rc != 0) {
        resp->status = 500;
    } else {
        resp->status = (result.status == VM_OK) ? 200 : 400;
    }
    snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
    free(entries);
}


}

static void respond_dialog(const kolibri_config_t *cfg, const char *body, size_t body_len, http_response_t *resp) {
    struct json_object *root = NULL;
    const char *input = NULL;
    int rc = parse_digits_array(body, body_len, &root, &input);
    if (rc != PARSE_OK) {
        switch (rc) {
        case PARSE_ERR_JSON:
            set_response(resp, 400, "application/json", "{\"error\":\"invalid json\"}");
            break;
        case PARSE_ERR_ROOT_TYPE:
            set_response(resp, 400, "application/json", "{\"error\":\"request must be object\"}");
            break;
        case PARSE_ERR_MISSING_FIELD:
            set_response(resp, 400, "application/json", "{\"error\":\"missing input\"}");
            break;
        case PARSE_ERR_FIELD_TYPE:
            set_response(resp, 400, "application/json", "{\"error\":\"input must be string\"}");
            break;
        case PARSE_ERR_OOM:
            set_response(resp, 500, "application/json", "{\"error\":\"oom\"}");
            break;
        default:
            set_response(resp, 400, "application/json", "{\"error\":\"invalid input\"}");
            break;
        }
        return;
    }
    uint8_t *digits = NULL;
    size_t digits_len = 0;
    rc = build_digits_from_input(input, &digits, &digits_len);
    json_object_put(root);
    if (rc != PARSE_OK) {
        if (rc == PARSE_ERR_OOM) {
            set_response(resp, 500, "application/json", "{\"error\":\"oom\"}");
        } else {
            set_response(resp, 400, "application/json", "{\"error\":\"unsupported expression\"}");
        }
        return;
    }
    struct byte_buffer bb = {0};
    if (compile_expression_program(digits, digits_len, &bb) != 0) {
        free(digits);
        set_response(resp, 400, "application/json", "{\"error\":\"unsupported expression\"}");
        return;
    }
    free(digits);


    const char *query = strchr(path, '?');
    if (!query) {
        set_response(resp, 400, "application/json", "{\"error\":\"missing query\"}");
        return -1;
    }
    query++;
    char prefix[128] = {0};
    size_t prefix_len = 0;
    int topk = 10;
    const char *p = query;
    while (*p) {
        if (strncmp(p, "prefix=", 7) == 0) {
            p += 7;
            size_t idx = 0;
            while (*p && *p != '&' && idx + 1 < sizeof(prefix)) {
                prefix[idx++] = *p++;
            }
            prefix[idx] = '\0';
            prefix_len = idx;
        } else if (strncmp(p, "k=", 2) == 0) {
            p += 2;
            topk = atoi(p);
            while (*p && *p != '&') {
                p++;
            }
        } else if (strncmp(p, "limit=", 6) == 0) {
            p += 6;
            topk = atoi(p);
            while (*p && *p != '&') {
                p++;
            }
        } else {
            while (*p && *p != '&') {
                p++;
            }
        }
        if (*p == '&') {
            p++;
        }
    }
    if (prefix_len == 0) {
        set_response(resp, 400, "application/json", "{\"error\":\"missing prefix\"}");
        return -1;
    }
    if (topk <= 0) {
        topk = 10;
    }
    uint8_t digits[128];
    for (size_t i = 0; i < prefix_len && i < sizeof(digits); ++i) {
        if (!isdigit((unsigned char)prefix[i])) {
            set_response(resp, 400, "application/json", "{\"error\":\"invalid prefix\"}");
            return -1;
        }
        digits[i] = (uint8_t)(prefix[i] - '0');
    }
    fkv_iter_t it = {0};
    if (fkv_get_prefix(digits, prefix_len, &it, (size_t)topk) != 0) {
        set_response(resp, 500, "application/json", "{\"error\":\"lookup failed\"}");
        return -1;
    }
    size_t cap = 256;
    char *json = malloc(cap);
    if (!json) {
        fkv_iter_free(&it);
        set_response(resp, 500, "application/json", "{\"error\":\"oom\"}");
        return -1;
    }
    size_t len = 0;

        }
        char *key_str = digits_to_string_dup(e->key, e->key_len);
        char *val_str = digits_to_string_dup(e->value, e->value_len);
        if (!key_str || !val_str) {
            free(key_str);
            free(val_str);
            free(json);
            fkv_iter_free(&it);
            set_response(resp, 500, "application/json", "{\"error\":\"oom\"}");
            return -1;
        }
        int rc = append_json(&json, &cap, &len, "%s{\\\"key\\\":\\\"%s\\\",\\\"value\\\":\\\"%s\\\"}",
                             (values_written == 0) ? "" : ",",
                             key_str,
                             val_str);
        free(key_str);
        free(val_str);
        if (rc != 0) {
            free(json);
            fkv_iter_free(&it);
            set_response(resp, 500, "application/json", "{\"error\":\"oom\"}");
            return -1;
        }
        values_written++;
    }
    if (append_json(&json, &cap, &len, "],\"programs\":[") != 0) {
        free(json);
        fkv_iter_free(&it);
        set_response(resp, 500, "application/json", "{\"error\":\"oom\"}");
        return -1;
    }

    resp->data = json;
    resp->len = len;
    resp->status = 200;
    snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
    fkv_iter_free(&it);
    return 0;
}

static int is_safe_path(const char *path) {
    return strstr(path, "..") == NULL;
}

static const char *mime_from_path(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) {
        return "text/html";
    }
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    return "text/html";
}

static int serve_static_file(const char *path, http_response_t *resp) {
    if (!is_safe_path(path)) {
        return -1;
    }
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        return -1;
    }
    char full_path[PATH_MAX];
    int written;
    if (strcmp(path, "/") == 0) {
        written = snprintf(full_path, sizeof(full_path), "%s/%s/index.html", cwd, WEB_DIST_DIR);
    } else {
        written = snprintf(full_path, sizeof(full_path), "%s/%s%s", cwd, WEB_DIST_DIR, path);
    }
    if (written < 0 || (size_t)written >= sizeof(full_path)) {
        return -1;
    }
    FILE *fp = fopen(full_path, "rb");
    if (!fp) {
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    size_t read = fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    buf[read] = '\0';
    resp->data = buf;
    resp->len = read;
    resp->status = 200;
    snprintf(resp->content_type, sizeof(resp->content_type), "%s", mime_from_path(full_path));
    return 0;
}

int http_handle_request(const kolibri_config_t *cfg,
                        const char *method,
                        const char *path,
                        const char *body,
                        size_t body_len,
                        http_response_t *resp) {
    if (!cfg || !method || !path || !resp) {
        return -1;
    }


    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/dialog") == 0) {
        respond_dialog(cfg, body ? body : "", body_len, resp);
        return 0;
    }


        return 0;
    }

    if (strcmp(method, "GET") == 0) {
        if (serve_static_file(path, resp) == 0) {
            return 0;
        }
    }

    set_response(resp, 404, "application/json", "{\"error\":\"not found\"}");
    return -1;
}
