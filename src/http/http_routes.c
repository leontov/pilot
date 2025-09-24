#define _POSIX_C_SOURCE 200809L
#include "http/http_routes.h"

#include "blockchain.h"
#include "fkv/fkv.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define HTTP_METRICS_SLOW_THRESHOLD_MS 250ull

static uint64_t routes_start_time = 0;
static Blockchain *routes_blockchain = NULL;

typedef struct {
    uint64_t total_requests;
    uint64_t error_responses;
    uint64_t total_bytes_sent;
    uint64_t total_duration_ms;
    uint64_t max_latency_ms;
    uint64_t method_get;
    uint64_t method_post;
    uint64_t method_other;
    uint64_t route_health;
    uint64_t route_metrics;
    uint64_t route_dialog;
    uint64_t route_vm_run;
    uint64_t route_fkv_get;
    uint64_t route_other;
    uint64_t status_4xx;
    uint64_t status_5xx;
    uint64_t slow_requests;
    uint64_t last_request_ms;
} http_metrics_counters_t;

static pthread_mutex_t metrics_lock = PTHREAD_MUTEX_INITIALIZER;
static http_metrics_counters_t metrics = {0};

static char *duplicate_string(const char *src) {
    size_t len = strlen(src);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}

void http_routes_metrics_record(const char *method,
                                const char *path,
                                int status,
                                uint64_t duration_ms,
                                size_t bytes_sent) {
    if (status == 0) {
        status = 200;
    }
    uint64_t now = now_ms();

    pthread_mutex_lock(&metrics_lock);
    metrics.total_requests++;
    metrics.total_bytes_sent += (uint64_t)bytes_sent;
    metrics.total_duration_ms += duration_ms;
    if (duration_ms > metrics.max_latency_ms) {
        metrics.max_latency_ms = duration_ms;
    }
    if (status >= 500) {
        metrics.error_responses++;
        metrics.status_5xx++;
    } else if (status >= 400) {
        metrics.error_responses++;
        metrics.status_4xx++;
    }
    if (duration_ms >= HTTP_METRICS_SLOW_THRESHOLD_MS) {
        metrics.slow_requests++;
    }
    metrics.last_request_ms = now;

    if (method) {
        if (strcmp(method, "GET") == 0) {
            metrics.method_get++;
        } else if (strcmp(method, "POST") == 0) {
            metrics.method_post++;
        } else {
            metrics.method_other++;
        }
    } else {
        metrics.method_other++;
    }

    if (path) {
        if (strcmp(path, "/api/v1/health") == 0) {
            metrics.route_health++;
        } else if (strcmp(path, "/api/v1/metrics") == 0) {
            metrics.route_metrics++;
        } else if (strcmp(path, "/api/v1/dialog") == 0) {
            metrics.route_dialog++;
        } else if (strcmp(path, "/api/v1/vm/run") == 0) {
            metrics.route_vm_run++;
        } else if (strcmp(path, "/api/v1/fkv/get") == 0) {
            metrics.route_fkv_get++;
        } else {
            metrics.route_other++;
        }
    } else {
        metrics.route_other++;
    }

    pthread_mutex_unlock(&metrics_lock);
}

static int respond_json(http_response_t *resp, const char *json, int status) {
    if (!resp || !json) {
        return -1;
    }
    char *data = duplicate_string(json);
    if (!data) {
        return -1;
    }
    resp->data = data;
    resp->len = strlen(data);
    resp->status = status;
    snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
    return 0;
}

static int handle_health(http_response_t *resp) {
    uint64_t uptime_ms = routes_start_time ? (now_ms() - routes_start_time) : 0;
    http_metrics_counters_t snapshot;
    pthread_mutex_lock(&metrics_lock);
    snapshot = metrics;
    pthread_mutex_unlock(&metrics_lock);
    size_t block_height = blockchain_get_block_count(routes_blockchain);
    size_t fkv_entries = fkv_entry_count();
    char buffer[256];
    int written = snprintf(buffer,
                           sizeof(buffer),
                           "{\"uptime_ms\":%llu,\"blockchain_attached\":%s,\"block_height\":%llu,\"fkv_entries\":%llu,\"total_requests\":%llu}",
                           (unsigned long long)uptime_ms,
                           routes_blockchain ? "true" : "false",
                           (unsigned long long)block_height,
                           (unsigned long long)fkv_entries,
                           (unsigned long long)snapshot.total_requests);
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        return -1;
    }
    return respond_json(resp, buffer, 200);
}

static int handle_metrics(http_response_t *resp) {
    http_metrics_counters_t snapshot;
    pthread_mutex_lock(&metrics_lock);
    snapshot = metrics;
    pthread_mutex_unlock(&metrics_lock);

    uint64_t uptime_ms = routes_start_time ? (now_ms() - routes_start_time) : 0;
    double avg_latency = snapshot.total_requests
                             ? (double)snapshot.total_duration_ms / (double)snapshot.total_requests
                             : 0.0;
    double slow_ratio = snapshot.total_requests
                            ? (double)snapshot.slow_requests / (double)snapshot.total_requests
                            : 0.0;
    double avg_rps = uptime_ms > 0 ? ((double)snapshot.total_requests * 1000.0) / (double)uptime_ms : 0.0;
    size_t fkv_entries = fkv_entry_count();
    size_t block_height = blockchain_get_block_count(routes_blockchain);

    char buffer[1024];
    int written = snprintf(
        buffer,
        sizeof(buffer),
        "{"
        "\"uptime_ms\":%llu,"
        "\"total_requests\":%llu,"
        "\"error_responses\":%llu,"
        "\"avg_latency_ms\":%.3f,"
        "\"max_latency_ms\":%llu,"
        "\"avg_rps\":%.3f,"
        "\"slow_threshold_ms\":%llu,"
        "\"slow_requests\":%llu,"
        "\"slow_requests_ratio\":%.4f,"
        "\"bytes_sent\":%llu,"
        "\"last_request_ms\":%llu,"
        "\"status\":{\"4xx\":%llu,\"5xx\":%llu},"
        "\"by_method\":{\"GET\":%llu,\"POST\":%llu,\"OTHER\":%llu},"
        "\"by_route\":{\"/api/v1/health\":%llu,\"/api/v1/metrics\":%llu,\"/api/v1/dialog\":%llu,\"/api/v1/vm/run\":%llu,\"/api/v1/fkv/get\":%llu,\"other\":%llu},"
        "\"fkv_entries\":%llu,"
        "\"block_height\":%llu"
        "}",
        (unsigned long long)uptime_ms,
        (unsigned long long)snapshot.total_requests,
        (unsigned long long)snapshot.error_responses,
        avg_latency,
        (unsigned long long)snapshot.max_latency_ms,
        avg_rps,
        (unsigned long long)HTTP_METRICS_SLOW_THRESHOLD_MS,
        (unsigned long long)snapshot.slow_requests,
        slow_ratio,
        (unsigned long long)snapshot.total_bytes_sent,
        (unsigned long long)snapshot.last_request_ms,
        (unsigned long long)snapshot.status_4xx,
        (unsigned long long)snapshot.status_5xx,
        (unsigned long long)snapshot.method_get,
        (unsigned long long)snapshot.method_post,
        (unsigned long long)snapshot.method_other,
        (unsigned long long)snapshot.route_health,
        (unsigned long long)snapshot.route_metrics,
        (unsigned long long)snapshot.route_dialog,
        (unsigned long long)snapshot.route_vm_run,
        (unsigned long long)snapshot.route_fkv_get,
        (unsigned long long)snapshot.route_other,
        (unsigned long long)fkv_entries,
        (unsigned long long)block_height);
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        return -1;
    }

    return respond_json(resp, buffer, 200);
}

static int handle_dialog(const char *body, size_t body_len, http_response_t *resp) {
    (void)body_len;
    const char *reply = "{\"answer\":\"Kolibri is online\"}";
    if (body && strstr(body, "ping")) {
        reply = "{\"answer\":\"pong\"}";
    }
    return respond_json(resp, reply, 200);
}

int http_handle_request(const kolibri_config_t *cfg,
                        const char *method,
                        const char *path,
                        const char *body,
                        size_t body_len,
                        http_response_t *resp) {
    (void)cfg;
    if (!method || !path || !resp) {
        return -1;
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/health") == 0) {
        return handle_health(resp);
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/v1/metrics") == 0) {
        return handle_metrics(resp);
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/v1/dialog") == 0) {
        return handle_dialog(body, body_len, resp);
    }
    const char *not_found = "{\"error\":\"not_found\"}";
    return respond_json(resp, not_found, 404);
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

void http_routes_set_start_time(uint64_t ms_since_epoch) {
    routes_start_time = ms_since_epoch;
}

void http_routes_set_blockchain(Blockchain *chain) {
    routes_blockchain = chain;
}
