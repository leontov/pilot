#include "http/http_routes.h"
#include "util/config.h"
#include "fkv/fkv.h"

#include <assert.h>
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
    assert(strstr(resp.data, "\"result\":5"));
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
    assert(strstr(resp.data, "\"answer\":\"15\""));
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
    assert(strstr(resp.data, "\"key\":\"123\""));
    http_response_free(&resp);
}

int main(void) {
    kolibri_config_t cfg;
    init_config(&cfg);

    assert(fkv_init() == 0);
    test_vm_run_route(&cfg);
    fkv_shutdown();

    assert(fkv_init() == 0);
    test_dialog_route(&cfg);
    fkv_shutdown();

    assert(fkv_init() == 0);
    test_fkv_get_route(&cfg);
    fkv_shutdown();

    printf("http route tests passed\n");
    return 0;
}
