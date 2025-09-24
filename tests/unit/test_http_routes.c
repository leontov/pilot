/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "blockchain.h"
#include "fkv/fkv.h"
#include "http/http_routes.h"
#include "util/config.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void init_config(kolibri_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->http.host, sizeof(cfg->http.host), "127.0.0.1");
    cfg->http.port = 9000;
    cfg->http.max_body_size = 1024 * 1024;
    cfg->vm.max_steps = 256;
    cfg->vm.max_stack = 64;
    cfg->vm.trace_depth = 32;
}

static void test_vm_run_route(const kolibri_config_t *cfg) {
    const char *body = "{\"program\":[1,2,1,3,2,18]}";
    http_response_t resp = {0};

    int rc = http_handle_request(cfg,
                                 "POST",
                                 "/api/v1/vm/run",
                                 body,
                                 strlen(body),
                                 &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"result\":5") != NULL);

    http_response_free(&resp);
}

static void test_dialog_route(const kolibri_config_t *cfg) {
    const char *body = "{\"input\":\"7+8\"}";
    http_response_t resp = {0};

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
    uint8_t key[] = {1, 2, 3};
    uint8_t value[] = {4, 5};
    assert(fkv_put(key, sizeof(key), value, sizeof(value), FKV_ENTRY_TYPE_VALUE) == 0);

    http_response_t resp = {0};
    int rc = http_handle_request(cfg,
                                 "GET",
                                 "/api/v1/fkv/get?prefix=12&limit=1",
                                 NULL,
                                 0,
                                 &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"key\":\"123\"") != NULL);

    http_response_free(&resp);
}

static void test_chain_submit_route(const kolibri_config_t *cfg) {
    Blockchain *chain = blockchain_create();
    assert(chain != NULL);
    http_routes_set_blockchain(chain);

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
    assert(resp.data != NULL);

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
    assert(strstr(resp.data, "\"status\":\"accepted\"") != NULL);
    assert(chain->block_count >= 1);

    http_response_free(&resp);

    const char *missing_body = "{\"program_id\":\"prog-999999\"}";
    resp = (http_response_t){0};
    rc = http_handle_request(cfg,
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

    http_routes_set_blockchain(NULL);
    blockchain_destroy(chain);
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
