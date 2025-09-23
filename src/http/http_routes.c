#include "http/http_routes.h"

#include "util/log.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define JSON_CONTENT "application/json"

static uint64_t server_start_ms = 0;

static uint64_t now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull;
}

static void set_response(http_response_t *resp, int status, const char *content_type, const char *body) {
    if (!resp || !body || !content_type) {
        return;
    }
    size_t len = strlen(body);
    char *copy = malloc(len + 1);
    if (!copy) {
        log_error("failed to allocate response body");
        resp->data = NULL;
        resp->len = 0;
        resp->status = 500;
        snprintf(resp->content_type, sizeof(resp->content_type), "text/plain");
        return;
    }
    memcpy(copy, body, len + 1);
    resp->data = copy;
    resp->len = len;
    resp->status = status;
    snprintf(resp->content_type, sizeof(resp->content_type), "%s", content_type);
}

void http_routes_set_start_time(uint64_t ms_since_epoch) {
    server_start_ms = ms_since_epoch;
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

static void respond_health(const kolibri_config_t *cfg, http_response_t *resp) {
    (void)cfg;
    uint64_t now = now_ms();
    uint64_t uptime = server_start_ms ? (now - server_start_ms) : 0;
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"uptime_ms\":%llu}", (unsigned long long)uptime);
    set_response(resp, 200, JSON_CONTENT, buf);
}

static void respond_metrics(const kolibri_config_t *cfg, http_response_t *resp) {
    if (!cfg) {
        set_response(resp, 500, JSON_CONTENT, "{\"error\":\"config missing\"}");
        return;
    }
    uint64_t now = now_ms();
    uint64_t uptime = server_start_ms ? (now - server_start_ms) : 0;
    char buf[256];
    snprintf(buf,
             sizeof(buf),
             "{\"uptime_ms\":%llu,\"vm\":{\"max_steps\":%u,\"max_stack\":%u,\"trace_depth\":%u}}",
             (unsigned long long)uptime,
             cfg->vm.max_steps,
             cfg->vm.max_stack,
             cfg->vm.trace_depth);
    set_response(resp, 200, JSON_CONTENT, buf);
}

static void respond_not_found(http_response_t *resp) {
    set_response(resp, 404, JSON_CONTENT, "{\"error\":\"not found\"}");
}

static void respond_not_implemented(http_response_t *resp) {
    set_response(resp, 501, JSON_CONTENT, "{\"error\":\"not implemented\"}");
}

int http_handle_request(const kolibri_config_t *cfg,
                        const char *method,
                        const char *path,
                        const char *body,
                        size_t body_len,
                        http_response_t *resp) {
    (void)body;
    (void)body_len;
    if (!cfg || !method || !path || !resp) {
        errno = EINVAL;
        return -1;
    }

    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/api/v1/health") == 0) {
            respond_health(cfg, resp);
            return 0;
        }
        if (strcmp(path, "/api/v1/metrics") == 0) {
            respond_metrics(cfg, resp);
            return 0;
        }
        respond_not_found(resp);
        return -1;
    }

    if (strcmp(method, "POST") == 0) {
        respond_not_implemented(resp);
        return -1;
    }

    respond_not_found(resp);
    return -1;
}
