#define _POSIX_C_SOURCE 200809L

#include "http/http_server.h"

#include "http/http_routes.h"
#include "util/log.h"
#include "vm/vm.h"

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

typedef struct client_task_s {
    int client_fd;
    struct client_task_s *next;
} client_task_t;

typedef struct {
    int sockfd;
    pthread_t accept_thread;
    pthread_t *worker_threads;
    size_t worker_count;
    int running;
    int stop_accept;
    int stop_workers;
    kolibri_config_t cfg;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    client_task_t *queue_head;
    client_task_t *queue_tail;
} server_state_t;

static server_state_t server = {
    .sockfd = -1,
    .accept_thread = 0,
    .worker_threads = NULL,
    .worker_count = 0,
    .running = 0,
    .stop_accept = 0,
    .stop_workers = 0,
    .queue_mutex = PTHREAD_MUTEX_INITIALIZER,
    .queue_cond = PTHREAD_COND_INITIALIZER,
    .queue_head = NULL,
    .queue_tail = NULL,
};

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

static int read_request(int client,
                        char **out_buffer,
                        size_t *out_len,
                        size_t *out_header_len,
                        size_t max_body) {
    if (!out_buffer || !out_len || !out_header_len) {
        return -1;
    }

    size_t capacity = RECV_BUFFER;
    char *buffer = calloc(1, capacity + 1);
    if (!buffer) {
        return -1;
    }

    size_t total = 0;
    size_t header_len = 0;
    size_t content_length = 0;
    int have_content_length = 0;

    while (1) {
        if (total >= capacity) {
            size_t new_capacity = capacity * 2;
            char *tmp = realloc(buffer, new_capacity + 1);
            if (!tmp) {
                free(buffer);
                return -1;
            }
            buffer = tmp;
            capacity = new_capacity;
        }

        ssize_t n = recv(client, buffer + total, capacity - total, 0);
        if (n <= 0) {
            free(buffer);
            return -1;
        }
        total += (size_t)n;
        buffer[total] = '\0';

        if (header_len == 0) {
            char *headers_end = strstr(buffer, "\r\n\r\n");
            if (headers_end) {
                header_len = (size_t)(headers_end - buffer) + 4;
                size_t header_text_len = header_len >= 4 ? header_len - 4 : header_len;
                char saved = buffer[header_text_len];
                buffer[header_text_len] = '\0';
                const char *cl_hdr = find_header(buffer, "content-length:");
                buffer[header_text_len] = saved;
                if (cl_hdr) {
                    content_length = (size_t)strtoul(cl_hdr + 15, NULL, 10);
                    have_content_length = 1;
                }

                if (have_content_length && max_body > 0 && content_length > max_body) {
                    free(buffer);
                    return -2;
                }

                if (have_content_length && content_length > SIZE_MAX - header_len) {
                    free(buffer);
                    return -1;
                }

                size_t required = header_len + content_length;
                if (required > capacity) {
                    size_t new_capacity = required;
                    char *tmp = realloc(buffer, new_capacity + 1);
                    if (!tmp) {
                        free(buffer);
                        return -1;
                    }
                    buffer = tmp;
                    capacity = new_capacity;
                }

                if (total >= required) {
                    break;
                }
            }
        } else {
            size_t required = header_len + content_length;
            if (total >= required) {
                break;
            }
            if (required > capacity) {
                size_t new_capacity = required;
                char *tmp = realloc(buffer, new_capacity + 1);
                if (!tmp) {
                    free(buffer);
                    return -1;
                }
                buffer = tmp;
                capacity = new_capacity;
            }
        }
    }

    *out_buffer = buffer;
    *out_len = total;
    *out_header_len = header_len;
    return 0;
}

static const char *status_reason(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 413:
        return "Payload Too Large";
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

static void send_payload_too_large(int client) {
    http_response_t resp = {0};
    resp.status = 413;
    snprintf(resp.content_type, sizeof(resp.content_type), "application/json");
    resp.data = strdup("{\"error\":\"payload too large\"}");
    resp.len = strlen(resp.data);
    send_response(client, &resp);
    http_response_free(&resp);
}

static void handle_client(int client) {
    char *buffer = NULL;
    size_t received = 0;
    size_t header_len = 0;
    int rc = read_request(client,
                          &buffer,
                          &received,
                          &header_len,
                          server.cfg.http.max_body_size);
    if (rc == -2) {
        send_payload_too_large(client);
        return;
    }
    if (rc != 0) {
        return;
    }

    char method[8];
    char path[256];
    if (sscanf(buffer, "%7s %255s", method, path) != 2) {
        free(buffer);
        return;
    }

    if (header_len == 0 || header_len > received) {
        free(buffer);
        return;
    }

    size_t body_len = received - header_len;
    char *body = buffer + header_len;

    if (server.cfg.http.max_body_size > 0 && body_len > server.cfg.http.max_body_size) {
        send_payload_too_large(client);
        free(buffer);
        return;
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
    free(buffer);
}

static size_t determine_worker_count(void) {
    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    if (nproc < 1) {
        return 4;
    }
    if (nproc > 32) {
        return 32;
    }
    return (size_t)nproc;
}

static void enqueue_client(int client_fd) {
    client_task_t *task = malloc(sizeof(client_task_t));
    if (!task) {
        log_error("Failed to allocate client task");
        close(client_fd);
        return;
    }
    task->client_fd = client_fd;
    task->next = NULL;

    pthread_mutex_lock(&server.queue_mutex);
    if (server.queue_tail) {
        server.queue_tail->next = task;
    } else {
        server.queue_head = task;
    }
    server.queue_tail = task;
    pthread_cond_signal(&server.queue_cond);
    pthread_mutex_unlock(&server.queue_mutex);
}

static client_task_t *dequeue_client(void) {
    client_task_t *task = server.queue_head;
    if (task) {
        server.queue_head = task->next;
        if (!server.queue_head) {
            server.queue_tail = NULL;
        }
    }
    return task;
}

static void *worker_loop(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&server.queue_mutex);
        while (!server.queue_head && !server.stop_workers) {
            pthread_cond_wait(&server.queue_cond, &server.queue_mutex);
        }
        if (server.stop_workers && !server.queue_head) {
            pthread_mutex_unlock(&server.queue_mutex);
            break;
        }
        client_task_t *task = dequeue_client();
        pthread_mutex_unlock(&server.queue_mutex);
        if (!task) {
            continue;
        }
        int client = task->client_fd;
        free(task);
        handle_client(client);
        close(client);
    }
    return NULL;
}

static void *accept_loop(void *arg) {
    (void)arg;
    log_info("HTTP server listening on %s:%u", server.cfg.http.host, server.cfg.http.port);
    while (1) {
        pthread_mutex_lock(&server.queue_mutex);
        int should_stop = server.stop_accept;
        pthread_mutex_unlock(&server.queue_mutex);
        if (should_stop) {
            break;
        }

        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        int client = accept(server.sockfd, (struct sockaddr *)&addr, &addrlen);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EBADF || errno == EINVAL) {
                break;
            }
            log_error("accept failed: %s", strerror(errno));
            continue;
        }
        pthread_mutex_lock(&server.queue_mutex);
        if (server.stop_workers) {
            pthread_mutex_unlock(&server.queue_mutex);
            close(client);
            break;
        }
        pthread_mutex_unlock(&server.queue_mutex);
        enqueue_client(client);
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
    server.queue_head = NULL;
    server.queue_tail = NULL;
    server.stop_accept = 0;
    server.stop_workers = 0;
    server.running = 1;
    server.cfg = *cfg;
    size_t worker_count = determine_worker_count();
    pthread_t *workers = calloc(worker_count, sizeof(pthread_t));
    if (!workers) {
        close(server.sockfd);
        server.sockfd = -1;
        server.running = 0;
        return -1;
    }
    for (size_t i = 0; i < worker_count; ++i) {
        if (pthread_create(&workers[i], NULL, worker_loop, NULL) != 0) {
            pthread_mutex_lock(&server.queue_mutex);
            server.stop_workers = 1;
            pthread_cond_broadcast(&server.queue_cond);
            pthread_mutex_unlock(&server.queue_mutex);
            for (size_t j = 0; j < i; ++j) {
                pthread_join(workers[j], NULL);
            }
            free(workers);
            close(server.sockfd);
            server.sockfd = -1;
            server.running = 0;
            server.stop_workers = 0;
            return -1;
        }
    }
    server.worker_threads = workers;
    server.worker_count = worker_count;
    uint64_t start_ms = (uint64_t)time(NULL) * 1000ull;
    http_routes_set_start_time(start_ms);

    vm_set_seed(cfg->seed);
    if (pthread_create(&server.accept_thread, NULL, accept_loop, NULL) != 0) {
        pthread_mutex_lock(&server.queue_mutex);
        server.stop_accept = 1;
        server.stop_workers = 1;
        pthread_cond_broadcast(&server.queue_cond);
        pthread_mutex_unlock(&server.queue_mutex);
        for (size_t i = 0; i < server.worker_count; ++i) {
            pthread_join(server.worker_threads[i], NULL);
        }
        free(server.worker_threads);
        server.worker_threads = NULL;
        server.worker_count = 0;

        close(server.sockfd);
        server.sockfd = -1;
        server.running = 0;
        server.stop_accept = 0;
        server.stop_workers = 0;
        return -1;
    }
    return 0;
}

void http_server_stop(void) {
    if (!server.running) {
        return;
    }
    pthread_mutex_lock(&server.queue_mutex);
    server.running = 0;
    server.stop_accept = 1;
    server.stop_workers = 1;
    pthread_cond_broadcast(&server.queue_cond);
    pthread_mutex_unlock(&server.queue_mutex);
    shutdown(server.sockfd, SHUT_RDWR);
    close(server.sockfd);
    server.sockfd = -1;
    pthread_join(server.accept_thread, NULL);
    for (size_t i = 0; i < server.worker_count; ++i) {
        pthread_join(server.worker_threads[i], NULL);
    }
    free(server.worker_threads);
    server.worker_threads = NULL;
    server.worker_count = 0;

    pthread_mutex_lock(&server.queue_mutex);
    client_task_t *task = server.queue_head;
    server.queue_head = NULL;
    server.queue_tail = NULL;
    pthread_mutex_unlock(&server.queue_mutex);
    while (task) {
        client_task_t *next = task->next;
        close(task->client_fd);
        free(task);
        task = next;
    }

    server.stop_accept = 0;
    server.stop_workers = 0;
}
