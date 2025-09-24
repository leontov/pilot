/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "blockchain.h"
#include "fkv/fkv.h"
#include "http/http_routes.h"
#include "util/config.h"

#include <assert.h>
#include <json-c/json.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static kolibri_config_t test_config(void) {
    kolibri_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.vm.max_steps = 128;
    cfg.vm.max_stack = 64;
    cfg.vm.trace_depth = 16;
    cfg.seed = 42;
    return cfg;
}

int main(void) {
    kolibri_config_t cfg = test_config();
    http_response_t resp = {0};

    assert(fkv_init() == 0);
    Blockchain *chain = blockchain_create();
    assert(chain);
    http_routes_set_blockchain(chain);

    const char *vm_body = "{\"program\":[1,4,18]}";
    int rc = http_handle_request(&cfg,
                                 "POST",
                                 "/api/v1/vm/run",
                                 vm_body,
                                 strlen(vm_body),
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
                             "POST",
                             "/api/v1/chain/submit",
                             chain_body,
                             strlen(chain_body),
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
    http_response_free(&resp);

    const char *missing_body = "{\"program_id\":\"program-999\"}";
    rc = http_handle_request(&cfg,
                             "POST",
                             "/api/v1/chain/submit",
                             missing_body,
                             strlen(missing_body),
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
    http_response_free(&resp);

    http_routes_set_blockchain(NULL);
    blockchain_destroy(chain);
    fkv_shutdown();
    return 0;
}
