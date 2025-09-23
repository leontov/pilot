#include "http/http_routes.h"

#include "blockchain.h"
#include "fkv/fkv.h"
#include "util/log.h"
#include "vm/vm.h"

#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#define WEB_DIST_DIR "web/dist"

static uint64_t server_start_ms = 0;
static Blockchain *global_blockchain = NULL;
static unsigned long next_program_id = 1;

void http_routes_set_start_time(uint64_t ms_since_epoch) {
    server_start_ms = ms_since_epoch;
}

void http_routes_set_blockchain(Blockchain *chain) {
    global_blockchain = chain;
    next_program_id = 1;
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

static int parse_digits_array(const char *body, size_t body_len, uint8_t **out, size_t *out_len) {
    (void)body_len;
    size_t cap = 16;
    uint8_t *buf = malloc(cap);
    if (!buf) {
        return -1;
    }
    size_t count = 0;
    const char *start = strchr(body, '[');
    const char *end = strrchr(body, ']');
    if (!start || !end || end <= start) {
        free(buf);
        return -1;
    }
    start++;
    const char *ptr = start;
    while (ptr < end) {
        while (ptr < end && (isspace((unsigned char)*ptr) || *ptr == ',')) {
            ptr++;
        }
        if (ptr >= end) {
            break;
        }
        char *next = NULL;
        long v = strtol(ptr, &next, 10);
        if (ptr == next) {
            break;
        }
        if (v < 0 || v > 255) {
            free(buf);
            return -1;
        }
        if (count + 1 > cap) {
            if (ensure_capacity(&buf, &cap, count + 1) != 0) {
                free(buf);
                return -1;
            }
        }
        buf[count++] = (uint8_t)v;
        ptr = next;
    }
    *out = buf;
    *out_len = count;
    return 0;
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

static int parse_named_byte_array(const char *body,
                                  size_t body_len,
                                  const char *field,
                                  uint8_t **out,
                                  size_t *out_len) {
    (void)body_len;
    const char *field_key = strstr(body, field);
    if (!field_key) {
        return -1;
    }
    const char *start = strchr(field_key, '[');
    const char *end = strchr(field_key, ']');
    if (!start || !end || end <= start) {
        return -1;
    }
    size_t cap = 16;
    uint8_t *buf = malloc(cap);
    if (!buf) {
        return -1;
    }
    size_t count = 0;
    const char *ptr = start + 1;
    while (ptr < end) {
        while (ptr < end && (isspace((unsigned char)*ptr) || *ptr == ',')) {
            ptr++;
        }
        if (ptr >= end) {
            break;
        }
        char *next = NULL;
        long v = strtol(ptr, &next, 10);
        if (ptr == next) {
            break;
        }
        if (v < 0 || v > 255) {
            free(buf);
            return -1;
        }
        if (count + 1 > cap) {
            if (ensure_capacity(&buf, &cap, count + 1) != 0) {
                free(buf);
                return -1;
            }
        }
        buf[count++] = (uint8_t)v;
        ptr = next;
    }
    *out = buf;
    *out_len = count;
    return 0;
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

static void respond_status(const kolibri_config_t *cfg, http_response_t *resp) {
    uint64_t now = now_ms();
    uint64_t uptime = server_start_ms ? (now - server_start_ms) : 0;
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"uptime_ms\":%llu,\"vm_max_steps\":%u,\"vm_max_stack\":%u,\"seed\":%u}",
             (unsigned long long)uptime,
             cfg->vm.max_steps,
             cfg->vm.max_stack,
             cfg->seed);
    set_response(resp, 200, "application/json", buf);
}

static void respond_run(const kolibri_config_t *cfg, const uint8_t *prog_bytes, size_t prog_len, http_response_t *resp) {
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
    int written = snprintf(json, cap,
                           "{\"status\":%d,\"steps\":%u,\"result\":%llu,\"trace\":[",
                           result.status,
                           result.steps,
                           (unsigned long long)result.result);
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

static void respond_dialog(const kolibri_config_t *cfg, const char *body, size_t body_len, http_response_t *resp) {
    uint8_t *digits = NULL;
    size_t digits_len = 0;
    if (parse_digits_array(body, body_len, &digits, &digits_len) != 0) {
        set_response(resp, 400, "application/json", "{\"error\":\"invalid digits\"}");
        return;
    }
    struct byte_buffer bb = {0};
    if (compile_expression_program(digits, digits_len, &bb) != 0) {
        free(digits);
        set_response(resp, 400, "application/json", "{\"error\":\"unsupported expression\"}");
        return;
    }
    free(digits);

    respond_run(cfg, bb.data, bb.len, resp);
    free(bb.data);
}

static int respond_fkv_prefix(const char *path, http_response_t *resp) {
    const char *query = strchr(path, '?');
    if (!query) {
        set_response(resp, 400, "application/json", "{\"error\":\"missing query\"}");
        return -1;
    }
    query++;
    char key[128] = {0};
    size_t klen = 0;
    int topk = 10;
    const char *p = query;
    while (*p) {
        if (strncmp(p, "key=", 4) == 0) {
            p += 4;
            size_t idx = 0;
            while (*p && *p != '&' && idx + 1 < sizeof(key)) {
                key[idx++] = *p++;
            }
            key[idx] = '\0';
            klen = idx;
        } else if (strncmp(p, "k=", 2) == 0) {
            p += 2;
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
    if (klen == 0) {
        set_response(resp, 400, "application/json", "{\"error\":\"missing key\"}");
        return -1;
    }
    uint8_t digits[128];
    for (size_t i = 0; i < klen && i < sizeof(digits); ++i) {
        if (!isdigit((unsigned char)key[i])) {
            set_response(resp, 400, "application/json", "{\"error\":\"invalid key\"}");
            return -1;
        }
        digits[i] = (uint8_t)(key[i] - '0');
    }
    fkv_iter_t it = {0};
    if (fkv_get_prefix(digits, klen, &it, (size_t)topk) != 0) {
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
    len += snprintf(json + len, cap - len, "{\"entries\":[");
    for (size_t i = 0; i < it.count; ++i) {
        const fkv_entry_t *e = &it.entries[i];
        char key_buf[256];
        char val_buf[256];
        size_t key_written = 0;
        for (size_t j = 0; j < e->key_len && key_written + 1 < sizeof(key_buf); ++j) {
            key_buf[key_written++] = (char)('0' + e->key[j]);
        }
        key_buf[key_written] = '\0';
        size_t val_written = 0;
        for (size_t j = 0; j < e->value_len && val_written + 1 < sizeof(val_buf); ++j) {
            val_buf[val_written++] = (char)('0' + e->value[j]);
        }
        val_buf[val_written] = '\0';
        int needed = snprintf(NULL, 0, "%s{\"key\":\"%s\",\"value\":\"%s\"}",
                              (i == 0) ? "" : ",",
                              key_buf,
                              val_buf);
        if (len + (size_t)needed + 3 > cap) {
            size_t new_cap = cap * 2;
            while (new_cap < len + (size_t)needed + 3) {
                new_cap *= 2;
            }
            char *tmp = realloc(json, new_cap);
            if (!tmp) {
                free(json);
                fkv_iter_free(&it);
                set_response(resp, 500, "application/json", "{\"error\":\"oom\"}");
                return -1;
            }
            json = tmp;
            cap = new_cap;
        }
        len += snprintf(json + len, cap - len, "%s{\"key\":\"%s\",\"value\":\"%s\"}",
                        (i == 0) ? "" : ",",
                        key_buf,
                        val_buf);
    }
    len += snprintf(json + len, cap - len, "]}");
    resp->data = json;
    resp->len = strlen(json);
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

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/program/submit") == 0) {
        respond_program_submit(cfg, body ? body : "", body_len, resp);
        return 0;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/chain/submit") == 0) {
        respond_chain_submit(body ? body : "", body_len, resp);
        return 0;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/status") == 0) {
        respond_status(cfg, resp);
        return 0;
    }

    if (strcmp(method, "GET") == 0 && strncmp(path, "/fkv/prefix", 12) == 0) {
        return respond_fkv_prefix(path, resp);
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/dialog") == 0) {
        respond_dialog(cfg, body ? body : "", body_len, resp);
        return 0;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/run") == 0) {
        uint8_t *prog_bytes = NULL;
        size_t prog_len = 0;
        if (parse_program_array(body, body_len, &prog_bytes, &prog_len) != 0) {
            set_response(resp, 400, "application/json", "{\"error\":\"invalid program\"}");
            return -1;
        }
        respond_run(cfg, prog_bytes, prog_len, resp);
        free(prog_bytes);
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
