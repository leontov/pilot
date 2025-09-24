/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "blockchain.h"
#include "fkv/fkv.h"
#include "http/http_routes.h"
#include "util/config.h"

#include <assert.h>
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

    /* VM run endpoint */
    const char *vm_body = "{\"program\":\"2+2\"}";
    int rc = http_handle_request(&cfg,
                                 "POST",
                                 "/api/v1/vm/run",
                                 vm_body,
                                 strlen(vm_body),
                                 &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"result\":\"4\"") != NULL);
    http_response_free(&resp);

    /* Prepare FKV entries */
    uint8_t key_value[] = {1, 2, 3};
    uint8_t val_value[] = {4, 2};
    assert(fkv_put(key_value, 3, val_value, 2, FKV_ENTRY_TYPE_VALUE) == 0);
    uint8_t key_program[] = {1, 2, 4};
    uint8_t val_program[] = {9, 9};
    assert(fkv_put(key_program, 3, val_program, 2, FKV_ENTRY_TYPE_PROGRAM) == 0);

    rc = http_handle_request(&cfg,
                             "GET",
                             "/api/v1/fkv/get?prefix=12&limit=4",
                             NULL,
                             0,
                             &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"values\":[") != NULL);
    assert(strstr(resp.data, "\"programs\":[") != NULL);
    http_response_free(&resp);

    /* Program submission */
    const char *submit_body = "{\"program\":\"3+4\"}";
    rc = http_handle_request(&cfg,
                             "POST",
                             "/api/v1/program/submit",
                             submit_body,
                             strlen(submit_body),
                             &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    const char *program_id_start = strstr(resp.data, "\"program_id\":\"");
    assert(program_id_start);
    program_id_start += strlen("\"program_id\":\"");
    char program_id[64];
    size_t idx = 0;
    while (*program_id_start && *program_id_start != '"' && idx + 1 < sizeof(program_id)) {
        program_id[idx++] = *program_id_start++;
    }
    program_id[idx] = '\0';
    assert(idx > 0);
    http_response_free(&resp);

    /* Blockchain submission */
    Blockchain *chain = blockchain_create();
    assert(chain);
    http_routes_set_blockchain(chain);

    char chain_request[128];
    snprintf(chain_request,
             sizeof(chain_request),
             "{\"program_id\":\"%s\"}",
             program_id);
    rc = http_handle_request(&cfg,
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
    rc = http_handle_request(&cfg,
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
    fkv_shutdown();
    return 0;
}
