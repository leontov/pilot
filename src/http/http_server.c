#define _POSIX_C_SOURCE 200809L

#include "http/http_server.h"

#include "http/http_routes.h"
#include "util/log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <stdint.h>

#define RECV_BUFFER 8192

typedef struct {
    int sockfd;
    pthread_t thread;
    int running;
    kolibri_config_t cfg;
} server_state_t;

static server_state_t server = { .sockfd = -1, .thread = 0, .running = 0 };

static int create_listen_socket(const char *host, uint16_t port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0) {
        log_error("getaddrinfo failed: %s", gai_strerror(rc));
        return -1;
    }

    int sockfd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sockfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sockfd < 0) {
            continue;
        }
        int opt = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (bind(sockfd, ai->ai_addr, ai->ai_addrlen) == 0) {
            if (listen(sockfd, 16) == 0) {
                break;
            }
        }
        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(res);
    return sockfd;
}

static const char *find_header(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) {
            return p;
        }
    }
    return NULL;
}

static ssize_t read_request(int client, char *buffer, size_t buf_len) {
    ssize_t total = 0;
    while (total < (ssize_t)buf_len) {
        ssize_t n = recv(client, buffer + total, buf_len - total, 0);
        if (n <= 0) {
            return n;
        }
        total += n;
        if (strstr(buffer, "\r\n\r\n")) {
            break;
        }
    }
    return total;
}

static const char *status_reason(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 500:
    default:
        return "Internal Server Error";
    }
}

static void send_response(int client, const http_response_t *resp) {
    int status = resp->status ? resp->status : 200;
    const char *reason = status_reason(status);
    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
             status,
             reason,
             resp->content_type[0] ? resp->content_type : "application/json",
             resp->len);
    send(client, header, strlen(header), 0);
    if (resp->data && resp->len > 0) {
        send(client, resp->data, resp->len, 0);
    }
}

static void handle_client(int client) {
    char buffer[RECV_BUFFER + 1];
    memset(buffer, 0, sizeof(buffer));
    ssize_t received = read_request(client, buffer, RECV_BUFFER);
    if (received <= 0) {
        return;
    }

    char method[8];
    char path[256];
    if (sscanf(buffer, "%7s %255s", method, path) != 2) {
        return;
    }

    const char *headers_end = strstr(buffer, "\r\n\r\n");
    size_t header_len = headers_end ? (size_t)(headers_end - buffer + 4) : (size_t)received;
    size_t body_len = (size_t)received - header_len;
    char *body = buffer + header_len;

    const char *cl_hdr = find_header(buffer, "content-length:");
    size_t content_length = body_len;
    if (cl_hdr) {
        content_length = (size_t)strtoul(cl_hdr + 15, NULL, 10);
    }

    if (content_length > body_len) {
        size_t to_read = content_length - body_len;
        if (to_read + received > RECV_BUFFER) {
            to_read = RECV_BUFFER - received;
        }
        ssize_t n = recv(client, buffer + received, to_read, 0);
        if (n > 0) {
            received += n;
            body_len += (size_t)n;
        }
    }

    http_response_t resp = {0};
    if (http_handle_request(&server.cfg, method, path, body, body_len, &resp) != 0 && resp.status == 0) {
        resp.status = 500;
        snprintf(resp.content_type, sizeof(resp.content_type), "application/json");
        resp.data = strdup("{\"error\":\"internal\"}");
        resp.len = strlen(resp.data);
    }
    send_response(client, &resp);
    http_response_free(&resp);
}

static void *server_loop(void *arg) {
    (void)arg;
    log_info("HTTP server listening on %s:%u", server.cfg.http.host, server.cfg.http.port);
    while (server.running) {
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        int client = accept(server.sockfd, (struct sockaddr *)&addr, &addrlen);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        handle_client(client);
        close(client);
    }
    return NULL;
}

int http_server_start(const kolibri_config_t *cfg) {
    if (!cfg) {
        return -1;
    }
    if (server.running) {
        return 0;
    }
    server.sockfd = create_listen_socket(cfg->http.host, cfg->http.port);
    if (server.sockfd < 0) {
        return -1;
    }
    server.running = 1;
    server.cfg = *cfg;
    uint64_t start_ms = (uint64_t)time(NULL) * 1000ull;
    http_routes_set_start_time(start_ms);
    if (pthread_create(&server.thread, NULL, server_loop, NULL) != 0) {
        close(server.sockfd);
        server.sockfd = -1;
        server.running = 0;
        return -1;
    }
    return 0;
}

void http_server_stop(void) {
    if (!server.running) {
        return;
    }
    server.running = 0;
    shutdown(server.sockfd, SHUT_RDWR);
    close(server.sockfd);
    server.sockfd = -1;
    pthread_join(server.thread, NULL);
}
