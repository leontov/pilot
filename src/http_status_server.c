#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "kolibri_rules.h"
#include "kolibri_decimal_cell.h"
#include "kolibri_ai.h"

static int status_sock = -1;
static rules_t* status_rules = NULL;
static decimal_cell_t* status_cell = NULL;
static volatile sig_atomic_t* status_keep_running = NULL;
static KolibriAI* status_ai = NULL;

static void send_buffer(int client, const char* data, size_t len) {
    const char* ptr = data;
    size_t remaining = len;
    while (remaining > 0) {
        ssize_t written = write(client, ptr, remaining);
        if (written <= 0) {
            break;
        }
        ptr += (size_t)written;
        remaining -= (size_t)written;
    }
}

static void append_format(char* buffer, size_t size, size_t* offset,
                          const char* fmt, ...) {
    if (*offset >= size) {
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

static void handle_client(int client) {
    char req[1024] = {0};
    ssize_t r = read(client, req, sizeof(req) - 1);
    if (r <= 0) {
        close(client);
        return;
    }

    if (strstr(req, "GET /status")) {
        char resp[4096];
        size_t off = 0;

        append_format(resp, sizeof(resp), &off,
                      "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");

        uint8_t active_digits[DECIMAL_CELL_FANOUT] = {0};
        size_t active_count = decimal_cell_collect_active_children(
            status_cell, active_digits, DECIMAL_CELL_FANOUT);

        append_format(resp, sizeof(resp), &off,
                      "Digit: %u\nDepth: %u\nActive: %s\nLastSync: %llu\nSyncInterval: %llu\n",
                      status_cell->digit,
                      status_cell->depth,
                      status_cell->is_active ? "true" : "false",
                      (unsigned long long)status_cell->last_sync_time,
                      (unsigned long long)status_cell->sync_interval);
        append_format(resp, sizeof(resp), &off,
                      "ActiveChildren: %zu\nRules: %d\n",
                      active_count,
                      status_rules ? status_rules->count : 0);

        if (active_count > 0) {
            append_format(resp, sizeof(resp), &off, "ActiveChildDigits:");
            for (size_t i = 0; i < active_count; i++) {
                append_format(resp, sizeof(resp), &off, " %u", active_digits[i]);
            }
            append_format(resp, sizeof(resp), &off, "\n");
        }

        size_t len = off >= sizeof(resp) ? sizeof(resp) - 1 : off;
        send_buffer(client, resp, len);
    } else if (strstr(req, "GET /rules")) {
        char resp[4096];
        int header = snprintf(resp, sizeof(resp),
                              "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
        if (header < 0) {
            close(client);
            return;
        }
        size_t off = (size_t)header;
        if (off >= sizeof(resp)) {
            off = sizeof(resp) - 1;
        }
        for (int i = 0; i < status_rules->count && off < sizeof(resp) - 128; i++) {
            int body = snprintf(resp + off, sizeof(resp) - off,
                                "Rule %d: %s -> %s, tier=%d, fitness=%.3f\n",
                                i, status_rules->patterns[i], status_rules->actions[i],
                                status_rules->tiers[i], status_rules->fitness[i]);
            if (body <= 0) {
                break;
            }
            if ((size_t)body >= sizeof(resp) - off) {
                off = sizeof(resp) - 1;
                break;
            }
            off += (size_t)body;
        }
        send_buffer(client, resp, off);
    } else if (strstr(req, "GET /neighbors")) {
        char resp[4096];
        size_t off = 0;

        append_format(resp, sizeof(resp), &off,
                      "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");

        uint8_t active_digits[DECIMAL_CELL_FANOUT] = {0};
        size_t active_count = decimal_cell_collect_active_children(
            status_cell, active_digits, DECIMAL_CELL_FANOUT);

        if (active_count == 0) {
            append_format(resp, sizeof(resp), &off, "No active neighbors\n");
        } else {
            for (size_t i = 0; i < active_count; i++) {
                uint8_t digit = active_digits[i];
                const decimal_cell_t* child = status_cell->children[digit];
                append_format(resp, sizeof(resp), &off,
                              "Neighbor %zu: digit=%u child_active=%d child_last_sync=%llu child_last_state_change=%llu node_active=%s node_last_sync=%llu\n",
                              i,
                              digit,
                              status_cell->child_active[digit] ? 1 : 0,
                              (unsigned long long)status_cell->child_last_sync[digit],
                              (unsigned long long)status_cell->child_last_state_change[digit],
                              (child && child->is_active) ? "true" : "false",
                              child ? (unsigned long long)child->last_sync_time : 0ULL);
            }
        }

        size_t len = off >= sizeof(resp) ? sizeof(resp) - 1 : off;
        send_buffer(client, resp, len);
    } else if (strstr(req, "GET /api/v1/ai/snapshot")) {
        if (!status_ai) {
            const char resp[] =
                "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\n\r\nAI unavailable\n";
            send_buffer(client, resp, strlen(resp));
        } else {
            char *json = kolibri_ai_export_snapshot(status_ai);
            if (!json) {
                const char resp[] =
                    "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n\r\nFailed to export snapshot\n";
                send_buffer(client, resp, strlen(resp));
            } else {
                size_t len = strlen(json);
                char header[256];
                int header_len = snprintf(header, sizeof(header),
                                          "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n",
                                          len);
                if (header_len > 0) {
                    send_buffer(client, header, (size_t)header_len);
                }
                send_buffer(client, json, len);
                free(json);
            }
        }
    } else if (strstr(req, "POST /api/v1/ai/snapshot")) {
        if (!status_ai) {
            const char resp[] =
                "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\n\r\nAI unavailable\n";
            send_buffer(client, resp, strlen(resp));
        } else {
            char *body = strstr(req, "\r\n\r\n");
            if (body) {
                body += 4;
            } else {
                body = req + r;
            }
            int rc = kolibri_ai_import_snapshot(status_ai, body);
            if (rc != 0) {
                const char resp[] =
                    "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid snapshot\n";
                send_buffer(client, resp, strlen(resp));
            } else {
                const char resp[] =
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\":\"ok\"}\n";
                send_buffer(client, resp, strlen(resp));
            }
        }
    } else {
        const char resp[] =
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot found\n";
        send_buffer(client, resp, strlen(resp));
    }

    close(client);
}

int http_status_server_init(int port,
                            rules_t* rules,
                            decimal_cell_t* cell,
                            volatile sig_atomic_t* keep_running,
                            KolibriAI* ai) {
    if (!rules || !cell || !keep_running) {
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

    struct timeval tv = {
        .tv_sec = 1,
        .tv_usec = 0,
    };
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        close(sock);
        return -1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    if (listen(sock, 16) < 0) {
        close(sock);
        return -1;
    }

    status_sock = sock;
    status_rules = rules;
    status_cell = cell;
    status_keep_running = keep_running;
    status_ai = ai;

    return 0;
}

void http_status_server_run(void) {
    if (status_sock < 0 || !status_rules || !status_cell || !status_keep_running) {
        return;
    }

    while (*status_keep_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client = accept(status_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
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

    status_rules = NULL;
    status_cell = NULL;
    status_keep_running = NULL;
}
