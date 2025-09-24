#define _POSIX_C_SOURCE 200809L

#include "http/http_routes.h"

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#include "blockchain.h"
#include "kolibri_ai.h"

static uint64_t routes_start_time = 0;
static Blockchain *routes_blockchain = NULL;
static KolibriAI *routes_ai = NULL;
static const char *const k_error_template = "{\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}";
static const char *const k_health_template = "{\"status\":\"ok\",\"uptime_ms\":%llu}";
static const char *const k_metrics_template = "{\"metrics\":{\"poe\":%.6f,\"mdl\":%.6f}}";

static char *duplicate_string(const char *src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

void http_response_free(http_response_t *resp) {
    if (!resp) {
        return;
    }
    free(resp->data);
    resp->data = NULL;
    resp->len = 0;
    resp->status = 0;
    resp->content_type[0] = '\0';
}

static int respond_json(http_response_t *resp, const char *json, int status) {
    if (!resp || !json) {
        return -1;
    }
    char *payload = duplicate_string(json);
    if (!payload) {
        return -1;
    }
    http_response_free(resp);
    resp->data = payload;
    resp->len = strlen(payload);
    resp->status = status;
    snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
    return 0;
}

static int respond_error(http_response_t *resp, int status, const char *code, const char *message) {
    if (!code) {
        code = "internal_error";
    }
    if (!message) {
        message = "internal error";
    }
    char buffer[256];
    int written = snprintf(buffer, sizeof(buffer), k_error_template, code, message);
    if (written < 0 || written >= (int)sizeof(buffer)) {
        return -1;
    }
    return respond_json(resp, buffer, status);
}

static int handle_health(http_response_t *resp) {
    struct timespec ts;
    uint64_t now_ms = 0;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        now_ms = (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
    }
    uint64_t uptime_ms = 0;
    if (routes_start_time != 0 && now_ms >= routes_start_time) {
        uptime_ms = now_ms - routes_start_time;
    }
    char buffer[256];
    int written = snprintf(buffer,
                           sizeof(buffer),
                           k_health_template,
                           (unsigned long long)uptime_ms);
    if (written < 0 || written >= (int)sizeof(buffer)) {
        return -1;
    }
    return respond_json(resp, buffer, 200);
}

static int handle_metrics(http_response_t *resp) {
    (void)routes_blockchain;
    char buffer[256];
    int written = snprintf(buffer, sizeof(buffer), k_metrics_template, 0.0, 0.0);
    if (written < 0 || written >= (int)sizeof(buffer)) {
        return -1;
    }
    return respond_json(resp, buffer, 200);
}

static int handle_not_implemented(http_response_t *resp) {
    return respond_error(resp, 501, "not_implemented", "route not available");
}

int http_handle_request(const kolibri_config_t *cfg,
                        const char *method,
                        const char *path,
                        const char *body,
                        size_t body_len,
                        http_response_t *resp) {
    (void)cfg;
    (void)body;
    (void)body_len;
    if (!method || !path || !resp) {
        return -1;
    }

    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/api/v1/health") == 0) {
            return handle_health(resp);
        }
        if (strcmp(path, "/api/v1/metrics") == 0) {
            return handle_metrics(resp);
        }
        if (strncmp(path, "/api/v1/fkv/get", strlen("/api/v1/fkv/get")) == 0) {
            return handle_not_implemented(resp);
        }
    } else if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/v1/dialog") == 0 || strcmp(path, "/api/v1/vm/run") == 0 ||
            strcmp(path, "/api/v1/program/submit") == 0 || strcmp(path, "/api/v1/chain/submit") == 0) {
            return handle_not_implemented(resp);
        }
    }

    return respond_error(resp, 404, "not_found", "route not found");
}

void http_routes_set_start_time(uint64_t ms_since_epoch) {
    routes_start_time = ms_since_epoch;
}

void http_routes_set_blockchain(Blockchain *chain) {
    routes_blockchain = chain;
}

void http_routes_set_ai(KolibriAI *ai) {
    routes_ai = ai;
    (void)routes_ai;
}
