/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "blockchain.h"
#include "fkv/fkv.h"
#include "http/http_routes.h"
#include "kolibri_ai.h"
#include "util/config.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static kolibri_config_t test_config(void) {
    kolibri_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.vm.max_steps = 256;
    cfg.vm.max_stack = 128;
    cfg.vm.trace_depth = 32;
    cfg.seed = 42;
    return cfg;
}

static void assert_json_contains(const http_response_t *resp, const char *needle) {
    assert(resp);
    assert(resp->status == 200);
    assert(resp->data);
    assert(strstr(resp->data, needle) != NULL);
}

int main(void) {
    kolibri_config_t cfg = test_config();
    http_response_t resp = {0};

    http_routes_set_start_time((uint64_t)time(NULL) * 1000ull);

    KolibriAI *ai = kolibri_ai_create(&cfg);
    assert(ai);
    http_routes_set_ai(ai);

    Blockchain *chain = blockchain_create();
    assert(chain);
    http_routes_set_blockchain(chain);

    assert(fkv_init() == 0);
    uint8_t key_digits[] = {1, 2, 3};
    uint8_t val_digits[] = {4, 5, 6};
    assert(fkv_put(key_digits, sizeof(key_digits), val_digits, sizeof(val_digits), FKV_ENTRY_TYPE_VALUE) == 0);

    /* Health */
    assert(http_handle_request(&cfg, "GET", "/api/v1/health", NULL, 0, &resp) == 0);
    assert_json_contains(&resp, "\"status\":\"ok\"");
    http_response_free(&resp);

    /* VM run */
    const char *vm_body = "{\"bytecode\":[1,2,1,2,2,18],\"trace\":true}";
    assert(http_handle_request(&cfg, "POST", "/api/v1/vm/run", vm_body, strlen(vm_body), &resp) == 0);
    assert_json_contains(&resp, "\"status\":\"ok\"");
    assert_json_contains(&resp, "\"result\":\"4\"");
    http_response_free(&resp);

    /* FKV get */
    assert(http_handle_request(&cfg,
                               "GET",
                               "/api/v1/fkv/get?prefix=123&limit=4",
                               NULL,
                               0,
                               &resp) == 0);
    assert_json_contains(&resp, "\"values\":[");
    http_response_free(&resp);

    /* Program submit */
    const char *program_body =
        "{\"program_id\":\"prog-1\",\"content\":\"demo\",\"representation\":\"text\",\"effectiveness\":0.8}";
    assert(http_handle_request(&cfg,
                               "POST",
                               "/api/v1/program/submit",
                               program_body,
                               strlen(program_body),
                               &resp) == 0);
    assert_json_contains(&resp, "\"accepted\":true");
    http_response_free(&resp);

    /* Chain submit ok */
    const char *chain_body = "{\"program_id\":\"prog-1\"}";
    assert(http_handle_request(&cfg,
                               "POST",
                               "/api/v1/chain/submit",
                               chain_body,
                               strlen(chain_body),
                               &resp) == 0);
    assert_json_contains(&resp, "\"status\":\"accepted\"");
    http_response_free(&resp);

    /* Chain submit missing */
    const char *missing_body = "{\"program_id\":\"missing\"}";
    assert(http_handle_request(&cfg,
                               "POST",
                               "/api/v1/chain/submit",
                               missing_body,
                               strlen(missing_body),
                               &resp) == 0);
    assert(resp.status == 404);
    http_response_free(&resp);

    /* AI state */
    assert(http_handle_request(&cfg, "GET", "/api/v1/ai/state", NULL, 0, &resp) == 0);
    assert_json_contains(&resp, "\"formula_count\":");
    http_response_free(&resp);

    /* AI formulas */
    assert(http_handle_request(&cfg, "GET", "/api/v1/ai/formulas?limit=1", NULL, 0, &resp) == 0);
    assert_json_contains(&resp, "\"formulas\":[");
    http_response_free(&resp);

    /* AI snapshot round-trip */
    assert(http_handle_request(&cfg, "GET", "/api/v1/ai/snapshot", NULL, 0, &resp) == 0);
    size_t snapshot_len = resp.len;
    assert(resp.data);
    char *snapshot = malloc(snapshot_len + 1);
    assert(snapshot);
    memcpy(snapshot, resp.data, snapshot_len);
    snapshot[snapshot_len] = '\0';
    http_response_free(&resp);
    assert(http_handle_request(&cfg,
                               "POST",
                               "/api/v1/ai/snapshot",
                               snapshot,
                               snapshot_len,
                               &resp) == 0);
    assert_json_contains(&resp, "\"status\":\"ok\"");
    http_response_free(&resp);
    free(snapshot);

    /* Studio state */
    assert(http_handle_request(&cfg, "GET", "/api/v1/studio/state", NULL, 0, &resp) == 0);
    assert_json_contains(&resp, "\"http\":{");
    http_response_free(&resp);

    /* Metrics */
    assert(http_handle_request(&cfg, "GET", "/api/v1/metrics", NULL, 0, &resp) == 0);
    assert(strstr(resp.data, "kolibri_http_requests_total") != NULL);
    http_response_free(&resp);

    http_routes_set_ai(NULL);
    http_routes_set_blockchain(NULL);
    kolibri_ai_destroy(ai);
    blockchain_destroy(chain);
    fkv_shutdown();
    return 0;
}
