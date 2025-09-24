/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "fkv/fkv.h"
#include "http/http_routes.h"
#include "synthesis/formula_vm_eval.h"
#include "util/config.h"

#include <assert.h>
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

static void test_vm_run_route(const kolibri_config_t *cfg) {
    http_response_t resp = {0};

    const char *text_body = "{\"program\":\"2+2\"}";
    int rc = http_handle_request(cfg,
                                 "POST",
                                 "/api/v1/vm/run",
                                 text_body,
                                 strlen(text_body),
                                 &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"result\":\"4\"") != NULL);
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

    rc = http_handle_request(cfg,
                             "POST",
                             "/api/v1/vm/run",
                             bytecode_body,
                             strlen(bytecode_body),
                             &resp);
    assert(rc == 0);
    assert(resp.status == 200);
    assert(resp.data != NULL);
    assert(strstr(resp.data, "\"result\":\"8\"") != NULL);
    http_response_free(&resp);
}

static void test_fkv_get_route(const kolibri_config_t *cfg) {
    (void)cfg;
    uint8_t value_key[] = {1, 2, 3};
    uint8_t value_val[] = {4, 5};
    assert(fkv_put(value_key, sizeof(value_key), value_val, sizeof(value_val), FKV_ENTRY_TYPE_VALUE) == 0);

    uint8_t program_key[] = {1, 2, 9};
    uint8_t program_val[] = {7, 7};
    assert(fkv_put(program_key, sizeof(program_key), program_val, sizeof(program_val), FKV_ENTRY_TYPE_PROGRAM) == 0);

    http_response_t resp = {0};
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

int main(void) {
    kolibri_config_t cfg;
    init_config(&cfg);

    assert(fkv_init() == 0);
    test_vm_run_route(&cfg);
    test_fkv_get_route(&cfg);
    fkv_shutdown();

    printf("http route tests passed\n");
    return 0;
}
