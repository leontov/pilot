#include "blockchain.h"
#include "http/http_routes.h"
#include "kolibri_ai.h"
#include "util/config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static kolibri_config_t smoke_config(void) {
    kolibri_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.vm.max_steps = 128;
    cfg.vm.max_stack = 64;
    cfg.vm.trace_depth = 16;
    return cfg;
}

int main(void) {
    kolibri_config_t cfg = smoke_config();
    http_response_t resp = {0};

    KolibriAI *ai = kolibri_ai_create(&cfg);
    http_routes_set_ai(ai);
    Blockchain *chain = blockchain_create();
    http_routes_set_blockchain(chain);

    assert(http_handle_request(&cfg, "GET", "/api/v1/health", NULL, 0, &resp) == 0);
    assert(resp.status == 200);
    http_response_free(&resp);

    const char *vm_body = "{\"bytecode\":[1,2,1,2,2,18]}";
    assert(http_handle_request(&cfg, "POST", "/api/v1/vm/run", vm_body, strlen(vm_body), &resp) == 0);
    http_response_free(&resp);

    assert(http_handle_request(&cfg, "GET", "/api/v1/metrics", NULL, 0, &resp) == 0);
    assert(strstr(resp.data, "kolibri_http_requests_total"));
    http_response_free(&resp);

    http_routes_set_ai(NULL);
    http_routes_set_blockchain(NULL);
    kolibri_ai_destroy(ai);
    blockchain_destroy(chain);
    return 0;
}
