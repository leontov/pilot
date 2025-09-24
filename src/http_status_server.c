/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#define _POSIX_C_SOURCE 200809L

#include "http/status_server.h"

#include "fkv/fkv.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int status_sock = -1;
static volatile sig_atomic_t *status_keep_running = NULL;
static KolibriAI *status_ai = NULL;
static struct timespec status_started_at = {0, 0};

static void send_buffer(int client, const char *data, size_t len) {
    const char *ptr = data;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t written = send(client, ptr, remaining, 0);
        if (written <= 0) {
            break;
        }
        ptr += (size_t)written;
        remaining -= (size_t)written;
    }
}

static void append_format(char *buffer,
                          size_t size,
                          size_t *offset,
                          const char *fmt,
                          ...) {
    if (!buffer || !offset || *offset >= size) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer + *offset, size - *offset, fmt, args);
    va_end(args);

    if (written < 0) {
        *offset = size > 0 ? size - 1 : 0;
        return;
    }

    size_t remaining = size - *offset;
    if ((size_t)written >= remaining) {
        *offset = size > 0 ? size - 1 : 0;
    } else {
        *offset += (size_t)written;
    }
}

static uint64_t uptime_ms(void) {
    if (status_started_at.tv_sec == 0 && status_started_at.tv_nsec == 0) {
        return 0;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t start_ns = (uint64_t)status_started_at.tv_sec * 1000000000ull +
                        (uint64_t)status_started_at.tv_nsec;
    uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000ull +
                      (uint64_t)now.tv_nsec;
    if (now_ns < start_ns) {
        return 0;
    }
    return (now_ns - start_ns) / 1000000ull;
}

static char *base64_encode(const uint8_t *data, size_t len) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!data && len > 0) {
        return NULL;
    }
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = malloc(out_len + 1);
    if (!out) {
        return NULL;
    }
    size_t oi = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t octet_a = data[i];
        uint32_t octet_b = (i + 1 < len) ? data[i + 1] : 0;
        uint32_t octet_c = (i + 2 < len) ? data[i + 2] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        out[oi++] = table[(triple >> 18) & 0x3F];
        out[oi++] = table[(triple >> 12) & 0x3F];
        out[oi++] = (i + 1 < len) ? table[(triple >> 6) & 0x3F] : '=';
        out[oi++] = (i + 2 < len) ? table[triple & 0x3F] : '=';
    }
    out[out_len] = '\0';
    return out;
}

static void append_digits_json(char *buffer,
                               size_t size,
                               size_t *offset,
                               const uint8_t *digits,
                               size_t count) {
    append_format(buffer, size, offset, "[");
    for (size_t i = 0; i < count; ++i) {
        append_format(buffer, size, offset, "%s%u", i ? "," : "", digits[i]);
    }
    append_format(buffer, size, offset, "]");
}

static const char *entry_type_name(fkv_entry_type_t type) {
    switch (type) {
        case FKV_ENTRY_TYPE_VALUE:
            return "value";
        case FKV_ENTRY_TYPE_PROGRAM:
            return "program";
        default:
            return "unknown";
    }
}

static size_t append_fkv_entries_json(char *buffer, size_t size, size_t offset) {
    fkv_iter_t it = {0};
    int rc = fkv_get_prefix(NULL, 0, &it, 0);
    if (rc != 0) {
        append_format(buffer, size, &offset, "\"error\":\"fkv_unavailable\"");
        return offset;
    }

    append_format(buffer,
                  size,
                  &offset,
                  "\"topk_limit\":%zu,\"root_entries\":[",
                  fkv_get_topk_limit());

    for (size_t i = 0; i < it.count; ++i) {
        const fkv_entry_t *entry = &it.entries[i];
        char *value_b64 = base64_encode(entry->value, entry->value_len);
        append_format(buffer,
                      size,
                      &offset,
                      "%s{\"type\":\"%s\",\"priority\":%llu,\"key_digits\":",
                      i ? "," : "",
                      entry_type_name(entry->type),
                      (unsigned long long)entry->priority);
        append_digits_json(buffer, size, &offset, entry->key, entry->key_len);
        append_format(buffer, size, &offset, ",\"value_b64\":");
        if (value_b64) {
            append_format(buffer, size, &offset, "\"%s\"", value_b64);
        } else {
            append_format(buffer, size, &offset, "null");
        }
        append_format(buffer, size, &offset, "}");
        free(value_b64);
    }
    append_format(buffer, size, &offset, "]");
    fkv_iter_free(&it);
    return offset;
}

static void handle_status(int client) {
    char body[8192];
    size_t off = 0;
    append_format(body, sizeof(body), &off, "{");
    append_format(body,
                  sizeof(body),
                  &off,
                  "\"uptime_ms\":%llu,",
                  (unsigned long long)uptime_ms());
    append_format(body, sizeof(body), &off, "\"fkv\":{");
    off = append_fkv_entries_json(body, sizeof(body), off);
    append_format(body, sizeof(body), &off, "}");
    append_format(body, sizeof(body), &off, ",\"ai\":");
    char *ai_state = status_ai ? kolibri_ai_serialize_state(status_ai) : NULL;
    if (ai_state) {
        append_format(body, sizeof(body), &off, "%s", ai_state);
    } else {
        append_format(body, sizeof(body), &off, "null");
    }
    append_format(body, sizeof(body), &off, "}");

    size_t body_len = off >= sizeof(body) ? sizeof(body) - 1 : off;
    char header[256];
    int header_len = snprintf(header,
                              sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n\r\n",
                              body_len);
    if (header_len > 0) {
        send_buffer(client, header, (size_t)header_len);
    }
    send_buffer(client, body, body_len);
    free(ai_state);
}

static void handle_ai_state(int client) {
    char *json = status_ai ? kolibri_ai_serialize_state(status_ai) : NULL;
    if (!json) {
        const char resp[] =
            "HTTP/1.1 503 Service Unavailable\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 30\r\n"
            "Connection: close\r\n\r\n"
            "{\"error\":\"ai_unavailable\"}";
        send_buffer(client, resp, sizeof(resp) - 1);
        return;
    }
    size_t len = strlen(json);
    char header[256];
    int header_len = snprintf(header,
                              sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n\r\n",
                              len);
    if (header_len > 0) {
        send_buffer(client, header, (size_t)header_len);
    }
    send_buffer(client, json, len);
    free(json);
}

static void handle_ai_snapshot_get(int client) {
    if (!status_ai) {
        const char resp[] =
            "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\n"
            "Content-Length: 30\r\nConnection: close\r\n\r\n"
            "{\"error\":\"ai_unavailable\"}";
        send_buffer(client, resp, sizeof(resp) - 1);
        return;
    }
    char *json = kolibri_ai_export_snapshot(status_ai);
    if (!json) {
        const char resp[] =
            "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n"
            "Content-Length: 47\r\nConnection: close\r\n\r\n"
            "{\"error\":\"snapshot_export_failed\"}";
        send_buffer(client, resp, sizeof(resp) - 1);
        return;
    }
    size_t len = strlen(json);
    char header[256];
    int header_len = snprintf(header,
                              sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n\r\n",
                              len);
    if (header_len > 0) {
        send_buffer(client, header, (size_t)header_len);
    }
    send_buffer(client, json, len);
    free(json);
}

static void handle_ai_snapshot_post(int client, const char *body) {
    if (!status_ai) {
        const char resp[] =
            "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\n"
            "Content-Length: 30\r\nConnection: close\r\n\r\n"
            "{\"error\":\"ai_unavailable\"}";
        send_buffer(client, resp, sizeof(resp) - 1);
        return;
    }
    if (!body || body[0] == '\0') {
        const char resp[] =
            "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n"
            "Content-Length: 33\r\nConnection: close\r\n\r\n"
            "{\"error\":\"missing_snapshot\"}";
        send_buffer(client, resp, sizeof(resp) - 1);
        return;
    }
    int rc = kolibri_ai_import_snapshot(status_ai, body);
    if (rc != 0) {
        const char resp[] =
            "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n"
            "Content-Length: 35\r\nConnection: close\r\n\r\n"
            "{\"error\":\"invalid_snapshot\"}";
        send_buffer(client, resp, sizeof(resp) - 1);
        return;
    }
    const char resp[] =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: 15\r\nConnection: close\r\n\r\n"
        "{\"status\":\"ok\"}";
    send_buffer(client, resp, sizeof(resp) - 1);
}

static void handle_fkv_root(int client) {
    char body[8192];
    size_t off = 0;
    append_format(body, sizeof(body), &off, "{");
    off = append_fkv_entries_json(body, sizeof(body), off);
    append_format(body, sizeof(body), &off, "}");
    size_t body_len = off >= sizeof(body) ? sizeof(body) - 1 : off;
    char header[256];
    int header_len = snprintf(header,
                              sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n\r\n",
                              body_len);
    if (header_len > 0) {
        send_buffer(client, header, (size_t)header_len);
    }
    send_buffer(client, body, body_len);
}

static void handle_not_found(int client) {
    const char resp[] =
        "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n"
        "Content-Length: 33\r\nConnection: close\r\n\r\n"
        "{\"error\":\"resource_not_found\"}";
    send_buffer(client, resp, sizeof(resp) - 1);
}

static void handle_method_not_allowed(int client) {
    const char resp[] =
        "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: application/json\r\n"
        "Content-Length: 38\r\nConnection: close\r\nAllow: GET, POST\r\n\r\n"
        "{\"error\":\"method_not_allowed\"}";
    send_buffer(client, resp, sizeof(resp) - 1);
}

static void route_request(int client, const char *method, const char *path, const char *body) {
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/status") == 0) {
            handle_status(client);
            return;
        }
        if (strcmp(path, "/fkv/root") == 0) {
            handle_fkv_root(client);
            return;
        }
        if (strcmp(path, "/api/v1/ai/snapshot") == 0) {
            handle_ai_snapshot_get(client);
            return;
        }
        if (strcmp(path, "/api/v1/ai/state") == 0) {
            handle_ai_state(client);
            return;
        }
        handle_not_found(client);
        return;
    }
    if (strcmp(method, "POST") == 0) {
        if (strcmp(path, "/api/v1/ai/snapshot") == 0) {
            handle_ai_snapshot_post(client, body);
            return;
        }
        handle_not_found(client);
        return;
    }
    handle_method_not_allowed(client);
}

static void handle_client(int client) {
    char req[4096];
    ssize_t r = recv(client, req, sizeof(req) - 1, 0);
    if (r <= 0) {
        close(client);
        return;
    }
    req[r] = '\0';

    char method[8] = {0};
    char path[256] = {0};
    if (sscanf(req, "%7s %255s", method, path) != 2) {
        handle_method_not_allowed(client);
        close(client);
        return;
    }
    const char *body = strstr(req, "\r\n\r\n");
    if (body) {
        body += 4;
    }
    route_request(client, method, path, body);
    close(client);
}

int http_status_server_init(uint16_t port,
                            volatile sig_atomic_t *keep_running,
                            KolibriAI *ai) {
    if (!keep_running) {
        errno = EINVAL;
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(sock);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    if (listen(sock, 16) < 0) {
        close(sock);
        return -1;
    }

    status_sock = sock;
    status_keep_running = keep_running;
    status_ai = ai;
    clock_gettime(CLOCK_MONOTONIC, &status_started_at);
    return 0;
}

void http_status_server_run(void) {
    if (status_sock < 0 || !status_keep_running) {
        return;
    }

    while (*status_keep_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client = accept(status_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        handle_client(client);
    }
}

void http_status_server_shutdown(void) {
    if (status_sock >= 0) {
        shutdown(status_sock, SHUT_RDWR);
        close(status_sock);
        status_sock = -1;
    }
    status_keep_running = NULL;
    status_ai = NULL;
    status_started_at.tv_sec = 0;
    status_started_at.tv_nsec = 0;
}
