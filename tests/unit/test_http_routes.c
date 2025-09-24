/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "blockchain.h"
#include "fkv/fkv.h"
#include "http/http_routes.h"
#include "util/config.h"

#include <assert.h>
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
    assert(chain);
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
    http_response_free(&resp);

    http_routes_set_blockchain(NULL);
    blockchain_destroy(chain);
    return 0;
}
