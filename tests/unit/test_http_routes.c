/* Copyright (c) 2025 Кочуров Владислав Евгеньевич */


#include "blockchain.h"
#include "fkv/fkv.h"
#include "http/http_routes.h"
#include "synthesis/formula_vm_eval.h"
#include "util/config.h"
#include <assert.h>
#include <json-c/json.h>
#include <stddef.h>
#include <stdio.h>

#include <stdlib.h>
#include <string.h>

static kolibri_config_t test_config(void) {
    kolibri_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.vm.max_steps = 256;
    cfg.vm.max_stack = 128;
    cfg.vm.trace_depth = 16;
    cfg.seed = 42;
    return cfg;
}

static void insert_sample(const char *key_str, const char *val_str, fkv_entry_type_t type) {
    size_t klen = strlen(key_str);
    size_t vlen = strlen(val_str);
    uint8_t *kbuf = malloc(klen);
    uint8_t *vbuf = malloc(vlen);
    assert(kbuf && vbuf);
    for (size_t i = 0; i < klen; ++i) {
        kbuf[i] = (uint8_t)(key_str[i] - '0');
    }
    for (size_t i = 0; i < vlen; ++i) {
        vbuf[i] = (uint8_t)(val_str[i] - '0');
    }
    assert(fkv_put(kbuf, klen, vbuf, vlen, type) == 0);
    free(kbuf);
    free(vbuf);
}

static const char *find_field(const char *json, const char *key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) {
        return NULL;
    }
    pos += strlen(pattern);
    while (*pos && (*pos == ' ' || *pos == '\n' || *pos == '\r' || *pos == '\t')) {
        pos++;
    }
    if (*pos != ':') {
        return NULL;
    }
    pos++;
    while (*pos && (*pos == ' ' || *pos == '\n' || *pos == '\r' || *pos == '\t')) {
        pos++;
    }
    return *pos ? pos : NULL;
}

static int json_extract_string(const char *json, const char *key, char *out, size_t out_size) {
    const char *value = find_field(json, key);
    if (!value || *value != '"') {
        return -1;
    }
    value++;
    size_t written = 0;
    while (*value && *value != '"') {
        if (written + 1 >= out_size) {
            return -1;
        }
        out[written++] = *value++;
    }
    if (*value != '"') {
        return -1;
    }
    out[written] = '\0';
    return 0;
}

static int json_extract_bool(const char *json, const char *key) {
    const char *value = find_field(json, key);
    if (!value) {
        return -1;
    }
    if (strncmp(value, "true", 4) == 0) {
        return 1;
    }
    if (strncmp(value, "false", 5) == 0) {
        return 0;
    }
    return -1;
}

static int json_contains(const char *json, const char *needle) {
    return strstr(json, needle) != NULL;
}

int main(void) {
    kolibri_config_t cfg = test_config();
    http_response_t resp = (http_response_t){0};

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void init_config(kolibri_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->http.host, sizeof(cfg->http.host), "127.0.0.1");
    cfg->http.port = 9000;
    cfg->http.max_body_size = 1024 * 1024;
    cfg->vm.max_steps = 512;
    cfg->vm.max_stack = 128;
    cfg->vm.trace_depth = 32;
}

static void assert_missing_program_rejected(const kolibri_config_t *cfg) {
    const char *missing_body = "{\"program_id\":\"prog-999999\"}";
    http_response_t resp = (http_response_t){0};

    int rc = http_handle_request(cfg,
                                 "POST",
                                 "/api/v1/chain/submit",
                                 missing_body,
                                 strlen(missing_body),
                                 &resp);
    assert(rc == 0);
    assert(resp.status == 404);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"error\"") != NULL);

    http_response_free(&resp);
}

static void test_vm_run_route(const kolibri_config_t *cfg) {
    const char *body = "{\"program\":\"2+3\"}";
    http_response_t resp = (http_response_t){0};

    int rc = http_handle_request(cfg,
                                 "POST",
                                 "/api/v1/vm/run",
                                 body,
                                 strlen(body),
                                 &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"result\":\"5\"") != NULL);

    http_response_free(&resp);
}

static void test_dialog_route(const kolibri_config_t *cfg) {
    const char *body = "{\"input\":\"7+8\"}";
    http_response_t resp = (http_response_t){0};

    int rc = http_handle_request(cfg,
                                 "POST",
                                 "/api/v1/dialog",
                                 body,
                                 strlen(body),
                                 &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"answer\":\"15\"") != NULL);

    http_response_free(&resp);
}

static void test_fkv_get_route(const kolibri_config_t *cfg) {
    uint8_t value_key[] = {1, 2, 3};
    uint8_t value_val[] = {4, 5};
    assert(fkv_put(value_key, sizeof(value_key), value_val, sizeof(value_val), FKV_ENTRY_TYPE_VALUE) == 0);


    assert(fkv_init() == 0);

    uint8_t program_key[] = {1, 2, 9};
    uint8_t program_val[] = {7, 7};
    assert(fkv_put(program_key, sizeof(program_key), program_val, sizeof(program_val), FKV_ENTRY_TYPE_PROGRAM) == 0);

    http_response_t resp = (http_response_t){0};
    const char *path = "/api/v1/fkv/get?prefix=12&limit=5";
    int rc = http_handle_request(cfg, "GET", path, NULL, 0, &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"key\":\"123\"") != NULL);
    assert(strstr(resp.data, "\"value\":\"45\"") != NULL);
    assert(strstr(resp.data, "\"program\":\"77\"") != NULL);

    http_response_free(&resp);
}

static void test_chain_submit_route(const kolibri_config_t *cfg) {


    /* VM run happy path */
    const char *vm_body = "{\"program\":\"2+2\"}";
    assert(http_handle_request(&cfg,
                               "POST",
                               "/api/v1/vm/run",
                               vm_body,
                               strlen(vm_body),
                               &resp) == 0);
    assert(resp.status == 200);
    char buffer[128];
    assert(json_extract_string(resp.data, "status", buffer, sizeof(buffer)) == 0);
    assert(strcmp(buffer, "ok") == 0);
    assert(json_extract_string(resp.data, "result", buffer, sizeof(buffer)) == 0);
    assert(strcmp(buffer, "4") == 0);
    assert(json_contains(resp.data, "\"trace\":"));
    http_response_free(&resp);

    /* FKV prefix lookup */
    fkv_init();
    insert_sample("123", "45", FKV_ENTRY_TYPE_VALUE);
    insert_sample("124", "67", FKV_ENTRY_TYPE_VALUE);
    insert_sample("880", "987", FKV_ENTRY_TYPE_PROGRAM);

    assert(http_handle_request(&cfg,
                               "GET",
                               "/api/v1/fkv/get?prefix=12&limit=4",
                               NULL,
                               0,
                               &resp) == 0);
    assert(resp.status == 200);
    assert(json_contains(resp.data, "\"values\":["));
    assert(json_contains(resp.data, "\"key\":\"123\""));
    assert(json_contains(resp.data, "\"programs\":["));
    http_response_free(&resp);
    fkv_shutdown();

    /* Missing prefix should be rejected */
    assert(http_handle_request(&cfg,
                               "GET",
                               "/api/v1/fkv/get",
                               NULL,
                               0,
                               &resp) == 0);
    assert(resp.status == 400);
    http_response_free(&resp);

    /* Program submission and blockchain integration */
    Blockchain *chain = blockchain_create();
    assert(chain != NULL);
    http_routes_set_blockchain(chain);


    const char *submit_body = "{\"program\":\"2+3\"}";
    assert(http_handle_request(&cfg,
                               "POST",
                               "/api/v1/program/submit",
                               submit_body,
                               strlen(submit_body),
                               &resp) == 0);
    assert(resp.status == 200);
    assert(json_extract_string(resp.data, "program_id", buffer, sizeof(buffer)) == 0);
    assert(strstr(buffer, "program-") == buffer);
    assert(json_extract_bool(resp.data, "accepted") == 1);
    char chain_body[256];
    snprintf(chain_body, sizeof(chain_body), "{\"program_id\":\"%s\"}", buffer);
    http_response_free(&resp);

    assert(http_handle_request(&cfg,
                               "POST",
                               "/api/v1/chain/submit",
                               chain_body,
                               strlen(chain_body),
                               &resp) == 0);
    assert(resp.status == 200);
    assert(json_extract_string(resp.data, "status", buffer, sizeof(buffer)) == 0);
    assert(strcmp(buffer, "accepted") == 0);
    assert(chain->block_count == 1);
    http_response_free(&resp);

    /* Unknown program id */
    const char *missing_body = "{\"program_id\":\"program-missing\"}";
    assert(http_handle_request(&cfg,
                               "POST",
                               "/api/v1/chain/submit",
                               missing_body,
                               strlen(missing_body),
                               &resp) == 0);
    assert(resp.status == 404);


    const char *vm_body = "{\"program\":[1,4,18]}";
    int rc = http_handle_request(&cfg,
                                 "POST",
                                 "/api/v1/vm/run",
                                 vm_body,
                                 strlen(vm_body),

    const char *submit_body = "{\"program\":\"3+4\"}";
    http_response_t resp = (http_response_t){0};
    int rc = http_handle_request(cfg,
                                 "POST",
                                 "/api/v1/program/submit",
                                 submit_body,
                                 strlen(submit_body),

                                 &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    struct json_object *json = json_tokener_parse(resp.data);
    assert(json);
    struct json_object *status_obj = NULL;
    assert(json_object_object_get_ex(json, "status", &status_obj));
    assert(strcmp(json_object_get_string(status_obj), "ok") == 0);
    struct json_object *result_obj = NULL;
    assert(json_object_object_get_ex(json, "result", &result_obj));
    assert(json_object_get_int64(result_obj) == 4);
    json_object_put(json);
    http_response_free(&resp);

    const char *program_body = "{\"bytecode\":[1,4,18]}";
    rc = http_handle_request(&cfg,
                             "POST",
                             "/api/v1/program/submit",
                             program_body,
                             strlen(program_body),
                             &resp);
    assert(rc == 0);
    assert(resp.status == 200 || resp.status == 202);
    assert(resp.data != NULL);

    json = json_tokener_parse(resp.data);
    assert(json);
    struct json_object *poe_obj = NULL;
    assert(json_object_object_get_ex(json, "poe", &poe_obj));
    assert(json_object_get_double(poe_obj) >= 0.0);
    struct json_object *program_id_obj = NULL;
    assert(json_object_object_get_ex(json, "programId", &program_id_obj));
    const char *program_id = json_object_get_string(program_id_obj);
    char program_id_copy[64];
    strncpy(program_id_copy, program_id, sizeof(program_id_copy) - 1);
    program_id_copy[sizeof(program_id_copy) - 1] = '\0';
    json_object_put(json);
    http_response_free(&resp);
    assert(chain->block_count >= 1);

    char chain_body[128];
    snprintf(chain_body, sizeof(chain_body), "{\"program_id\":\"%s\"}", program_id_copy);
    rc = http_handle_request(&cfg,


    const char *program_id_start = strstr(resp.data, "\"program_id\":\"");
    assert(program_id_start != NULL);
    program_id_start += strlen("\"program_id\":\"");

    char program_id[64];
    size_t idx = 0;
    while (program_id_start[idx] && program_id_start[idx] != '"' && idx + 1 < sizeof(program_id)) {
        program_id[idx] = program_id_start[idx];
        idx++;
    }
    program_id[idx] = '\0';
    assert(idx > 0);

    http_response_free(&resp);

    char chain_request[128];
    snprintf(chain_request, sizeof(chain_request), "{\"program_id\":\"%s\"}", program_id);

    resp = (http_response_t){0};
    rc = http_handle_request(cfg,

                             "POST",
                             "/api/v1/chain/submit",
                             chain_request,
                             strlen(chain_request),
                             &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);

    json = json_tokener_parse(resp.data);
    assert(json);
    struct json_object *chain_status_obj = NULL;
    assert(json_object_object_get_ex(json, "status", &chain_status_obj));
    assert(strcmp(json_object_get_string(chain_status_obj), "accepted") == 0);
    json_object_put(json);

    assert(strstr(resp.data, "\"status\":\"accepted\"") != NULL);
    assert(chain->block_count >= 1);


    http_response_free(&resp);

    uint8_t *bytecode = NULL;
    size_t bytecode_len = 0;
    assert(formula_vm_compile_from_text("3+5", &bytecode, &bytecode_len) == 0);

    char bytecode_body[1024];
    size_t offset = 0;
    offset += snprintf(bytecode_body + offset, sizeof(bytecode_body) - offset, "{\"bytecode\":[");
    for (size_t i = 0; i < bytecode_len; ++i) {
        offset += snprintf(bytecode_body + offset,
                           sizeof(bytecode_body) - offset,
                           "%s%u",
                           (i > 0) ? "," : "",
                           (unsigned)bytecode[i]);
    }
    assert(offset < sizeof(bytecode_body));
    snprintf(bytecode_body + offset, sizeof(bytecode_body) - offset, "]}");
    free(bytecode);

    resp = (http_response_t){0};
    rc = http_handle_request(cfg,
                             "POST",
                             "/api/v1/vm/run",
                             bytecode_body,
                             strlen(bytecode_body),
                             &resp);
    assert(rc == 0);

    assert(resp.status == 404);
    http_response_free(&resp);

    uint8_t key_digits[] = {1, 2, 3};
    uint8_t value_digits[] = {4};
    assert(fkv_put(key_digits, sizeof(key_digits), value_digits, sizeof(value_digits), FKV_ENTRY_TYPE_VALUE) == 0);

    rc = http_handle_request(&cfg,
                             "GET",
                             "/api/v1/fkv/get?prefix=123",
                             NULL,
                             0,
                             &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    json = json_tokener_parse(resp.data);
    assert(json);
    struct json_object *values_obj = NULL;
    assert(json_object_object_get_ex(json, "values", &values_obj));
    assert(json_object_array_length(values_obj) >= 1);
    json_object_put(json);

    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"result\":\"8\"") != NULL);



    http_response_free(&resp);

    assert_missing_program_rejected(cfg);

    http_routes_set_blockchain(NULL);
    blockchain_destroy(chain);

    fkv_shutdown();

}

int main(void) {
    kolibri_config_t cfg;
    init_config(&cfg);

    http_routes_set_blockchain(NULL);

    assert(fkv_init() == 0);
    test_dialog_route(&cfg);
    fkv_shutdown();

    assert(fkv_init() == 0);
    test_vm_run_route(&cfg);
    fkv_shutdown();

    assert(fkv_init() == 0);
    test_fkv_get_route(&cfg);
    fkv_shutdown();

    assert(fkv_init() == 0);
    test_chain_submit_route(&cfg);
    fkv_shutdown();

    printf("http route tests passed\n");

    return 0;
}
