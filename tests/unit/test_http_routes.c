/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "blockchain.h"
#include "http/http_routes.h"
#include "util/config.h"

#include <assert.h>
#include <stddef.h>
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

    Blockchain *chain = blockchain_create();
    assert(chain);
    http_routes_set_blockchain(chain);

    const char *program_body = "{\"bytecode\":[1,2,3,4]}";
    int rc = http_handle_request(&cfg,
                                 "POST",
                                 "/api/v1/program/submit",
                                 program_body,
                                 strlen(program_body),
                                 &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"PoE\"") != NULL);
    http_response_free(&resp);
    assert(chain->block_count == 1);

    const char *chain_body = "{\"program_id\":\"program-1\"}";
    rc = http_handle_request(&cfg,
                             "POST",
                             "/api/v1/chain/submit",
                             chain_body,
                             strlen(chain_body),
                             &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"status\":\"accepted\"") != NULL);
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
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"status\":\"not_found\"") != NULL);
    http_response_free(&resp);

    http_routes_set_blockchain(NULL);
    blockchain_destroy(chain);
    return 0;
}
