#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>

#include "kolibri_rules.h"
#include "kolibri_decimal_cell.h"

static int status_sock = -1;
static rules_t* status_rules = NULL;
static decimal_cell_t* status_cell = NULL;
static volatile sig_atomic_t* status_keep_running = NULL;

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

static void handle_client(int client) {
    char req[1024] = {0};
    ssize_t r = read(client, req, sizeof(req) - 1);
    if (r <= 0) {
        close(client);
        return;
    }

    if (strstr(req, "GET /status")) {
        char resp[4096];
        int written = snprintf(resp, sizeof(resp),
                               "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nDigit: %d\nNeighbors: %u\nRules: %d\n",
                               status_cell->node_digit, status_cell->n_neighbors, status_rules->count);
        if (written > 0) {
            size_t len = (written >= (int)sizeof(resp)) ? sizeof(resp) - 1 : (size_t)written;
            send_buffer(client, resp, len);
        }
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
        for (int i = 0; i < status_cell->n_neighbors && off < sizeof(resp) - 128; i++) {
            int body = snprintf(resp + off, sizeof(resp) - off,
                                "Neighbor %d: digit=%d active=%d last_sync=%llu\n",
                                i, status_cell->neighbor_digits[i], status_cell->is_active[i],
                                (unsigned long long)status_cell->last_sync[i]);
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
    } else {
        const char resp[] =
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot found\n";
        send_buffer(client, resp, strlen(resp));
    }

    close(client);
}

int http_status_server_init(int port, rules_t* rules, decimal_cell_t* cell,
                            volatile sig_atomic_t* keep_running) {
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

    if (listen(sock, 5) < 0) {
        close(sock);
        return -1;
    }

    status_sock = sock;
    status_rules = rules;
    status_cell = cell;
    status_keep_running = keep_running;
    return 0;
}

void http_status_server_run(void) {
    if (status_sock < 0 || !status_keep_running) {
        return;
    }

    while (*status_keep_running) {
        if (status_sock < 0) {
            break;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(status_sock, &readfds);

        struct timeval timeout = {
            .tv_sec = 1,
            .tv_usec = 0,
        };

        int ready = select(status_sock + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (ready == 0) {
            continue;
        }

        if (!FD_ISSET(status_sock, &readfds)) {
            continue;
        }

        int client = accept(status_sock, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }

        handle_client(client);
    }

    status_rules = NULL;
    status_cell = NULL;
    status_keep_running = NULL;
}

void http_status_server_shutdown(void) {
    if (status_sock >= 0) {
        shutdown(status_sock, SHUT_RDWR);
        close(status_sock);
        status_sock = -1;
    }
}
