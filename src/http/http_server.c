/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#define _POSIX_C_SOURCE 200809L

#include "http/http_server.h"

#include "http/http_routes.h"
#include "util/jwt.h"
#include "util/key_manager.h"
#include "util/log.h"
#include "vm/vm.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
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
#include <strings.h>
#include <sys/stat.h>

#define RECV_BUFFER 8192

typedef struct client_task_s {
    int client_fd;
    SSL *ssl;
    struct client_task_s *next;
} client_task_t;

static ssize_t connection_recv(client_task_t *task, void *buf, size_t len);
static ssize_t connection_send(client_task_t *task, const void *buf, size_t len);

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
    SSL_CTX *ssl_ctx;
    pthread_mutex_t tls_mutex;
    time_t last_tls_reload;
    time_t cert_mtime;
    time_t key_mtime;
    key_file_t jwt_secret;
    int jwt_ready;
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
    .ssl_ctx = NULL,
    .tls_mutex = PTHREAD_MUTEX_INITIALIZER,
    .last_tls_reload = 0,
    .cert_mtime = 0,
    .key_mtime = 0,
    .jwt_ready = 0,
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

static void log_openssl_error(const char *context) {
    unsigned long err = 0;
    while ((err = ERR_get_error()) != 0) {
        log_error("%s: %s", context, ERR_error_string(err, NULL));
    }
}

static int get_file_mtime(const char *path, time_t *mtime_out) {
    struct stat st = {0};
    if (stat(path, &st) != 0) {
        log_error("http: failed to stat %s: %s", path, strerror(errno));
        return -1;
    }
    if (mtime_out) {
        *mtime_out = st.st_mtime;
    }
    return 0;
}

static int tls_reload_context(time_t cert_mtime, time_t key_mtime) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        log_openssl_error("http: SSL_CTX_new failed");
        return -1;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    if (SSL_CTX_use_certificate_file(ctx, server.cfg.http.tls_cert_path, SSL_FILETYPE_PEM) <= 0) {
        log_openssl_error("http: failed to load certificate");
        SSL_CTX_free(ctx);
        return -1;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, server.cfg.http.tls_key_path, SSL_FILETYPE_PEM) <= 0) {
        log_openssl_error("http: failed to load private key");
        SSL_CTX_free(ctx);
        return -1;
    }
    if (!SSL_CTX_check_private_key(ctx)) {
        log_error("http: TLS private key mismatch");
        SSL_CTX_free(ctx);
        return -1;
    }
    if (server.cfg.http.require_client_auth) {
        if (server.cfg.http.tls_client_ca_path[0] == '\0') {
            log_error("http: client auth required but no CA path provided");
            SSL_CTX_free(ctx);
            return -1;
        }
        if (SSL_CTX_load_verify_locations(ctx, server.cfg.http.tls_client_ca_path, NULL) != 1) {
            log_openssl_error("http: failed to load client CA");
            SSL_CTX_free(ctx);
            return -1;
        }
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
        SSL_CTX_set_verify_depth(ctx, 4);
    }

    pthread_mutex_lock(&server.tls_mutex);
    if (server.ssl_ctx) {
        SSL_CTX_free(server.ssl_ctx);
    }
    server.ssl_ctx = ctx;
    server.last_tls_reload = time(NULL);
    server.cert_mtime = cert_mtime;
    server.key_mtime = key_mtime;
    pthread_mutex_unlock(&server.tls_mutex);
    log_info("http: TLS context reloaded");
    return 0;
}

static int tls_ensure_context(void) {
    if (!server.cfg.http.enable_tls) {
        return 0;
    }
    if (server.cfg.http.tls_cert_path[0] == '\0' || server.cfg.http.tls_key_path[0] == '\0') {
        log_error("http: TLS enabled but certificate or key path missing");
        return -1;
    }
    time_t cert_mtime = 0;
    time_t key_mtime = 0;
    if (get_file_mtime(server.cfg.http.tls_cert_path, &cert_mtime) != 0 ||
        get_file_mtime(server.cfg.http.tls_key_path, &key_mtime) != 0) {
        return -1;
    }

    int need_reload = 0;
    pthread_mutex_lock(&server.tls_mutex);
    if (!server.ssl_ctx) {
        need_reload = 1;
    } else {
        time_t now = time(NULL);
        if ((server.cfg.http.key_rotation_interval_sec > 0 &&
             now - server.last_tls_reload >= (time_t)server.cfg.http.key_rotation_interval_sec) ||
            cert_mtime != server.cert_mtime ||
            key_mtime != server.key_mtime) {
            need_reload = 1;
        }
    }
    pthread_mutex_unlock(&server.tls_mutex);

    if (need_reload) {
        return tls_reload_context(cert_mtime, key_mtime);
    }
    return 0;
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

static int read_request(client_task_t *task,
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

        ssize_t n = connection_recv(task, buffer + total, capacity - total);
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

static void send_response(client_task_t *task, const http_response_t *resp) {
    int status = resp->status ? resp->status : 200;
    const char *reason = status_reason(status);
    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
             status,
             reason,
             resp->content_type[0] ? resp->content_type : "application/json",
             resp->len);
    connection_send(task, header, strlen(header));
    if (resp->data && resp->len > 0) {
        connection_send(task, resp->data, resp->len);
    }
}

static void send_payload_too_large(client_task_t *task) {
    http_response_t resp = {0};
    resp.status = 413;
    snprintf(resp.content_type, sizeof(resp.content_type), "application/json");
    resp.data = strdup("{\"error\":\"payload too large\"}");
    resp.len = strlen(resp.data);
    send_response(task, &resp);
    http_response_free(&resp);
}

static void send_unauthorized(client_task_t *task) {
    const char body[] = "{\"error\":\"unauthorized\"}";
    char header[512];
    int len = snprintf(header,
                       sizeof(header),
                       "HTTP/1.1 401 Unauthorized\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n"
                       "WWW-Authenticate: Bearer realm=\"Kolibri\"\r\n\r\n",
                       sizeof(body) - 1);
    connection_send(task, header, (size_t)len);
    connection_send(task, body, sizeof(body) - 1);
}

static int find_header_value(const char *headers,
                             size_t header_len,
                             const char *name,
                             char *out,
                             size_t out_size) {
    if (!headers || !name || !out || out_size == 0) {
        return -1;
    }
    const char *cur = headers;
    const char *end = headers + header_len;
    size_t name_len = strlen(name);
    while (cur < end) {
        const char *line_end = memchr(cur, '\n', (size_t)(end - cur));
        size_t line_len = line_end ? (size_t)(line_end - cur) : (size_t)(end - cur);
        if (line_len == 0) {
            if (!line_end) {
                break;
            }
            cur = line_end + 1;
            continue;
        }
        const char *colon = memchr(cur, ':', line_len);
        if (colon) {
            size_t key_len = (size_t)(colon - cur);
            while (key_len > 0 && isspace((unsigned char)cur[key_len - 1])) {
                key_len--;
            }
            size_t skip = 1;
            while (skip < line_len && isspace((unsigned char)colon[skip])) {
                skip++;
            }
            if (key_len == name_len && strncasecmp(cur, name, name_len) == 0) {
                size_t value_len = line_len - (colon - cur) - skip;
                if (value_len >= out_size) {
                    return -1;
                }
                memcpy(out, colon + skip, value_len);
                out[value_len] = '\0';
                return 0;
            }
        }
        if (!line_end) {
            break;
        }
        cur = line_end + 1;
    }
    return -1;
}

static void handle_client(client_task_t *task) {
    char *buffer = NULL;
    size_t received = 0;
    size_t header_len = 0;
    int rc = read_request(task,
                          &buffer,
                          &received,
                          &header_len,
                          server.cfg.http.max_body_size);
    if (rc == -2) {
        send_payload_too_large(task);
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
        send_payload_too_large(task);
        free(buffer);
        return;
    }

    int require_auth = http_route_requires_auth(method, path);
    if (require_auth) {
        char auth_header[512];
        if (find_header_value(buffer, header_len, "authorization", auth_header, sizeof(auth_header)) != 0) {
            send_unauthorized(task);
            free(buffer);
            return;
        }
        const char *bearer = NULL;
        if (strncasecmp(auth_header, "Bearer ", 7) == 0) {
            bearer = auth_header + 7;
        }
        if (!bearer || bearer[0] == '\0') {
            send_unauthorized(task);
            free(buffer);
            return;
        }
        if (!server.jwt_ready) {
            send_unauthorized(task);
            free(buffer);
            return;
        }
        const unsigned char *secret = NULL;
        size_t secret_len = 0;
        if (key_file_get(&server.jwt_secret, &secret, &secret_len) != 0) {
            log_error("http: failed to load JWT secret");
            send_unauthorized(task);
            free(buffer);
            return;
        }
        if (jwt_verify_hs256(bearer,
                             secret,
                             secret_len,
                             server.cfg.http.jwt_issuer,
                             server.cfg.http.jwt_audience,
                             NULL) != 0) {
            send_unauthorized(task);
            free(buffer);
            return;
        }
    }

    http_response_t resp = {0};
    if (http_handle_request(&server.cfg, method, path, body, body_len, &resp) != 0 && resp.status == 0) {
        resp.status = 500;
        snprintf(resp.content_type, sizeof(resp.content_type), "application/json");
        resp.data = strdup("{\"error\":\"internal\"}");
        resp.len = strlen(resp.data);
    }
    send_response(task, &resp);
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

static void enqueue_client(int client_fd, SSL *ssl) {
    client_task_t *task = malloc(sizeof(client_task_t));
    if (!task) {
        log_error("Failed to allocate client task");
        close(client_fd);
        return;
    }
    task->client_fd = client_fd;
    task->ssl = ssl;
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

static void connection_close(client_task_t *task) {
    if (!task) {
        return;
    }
    if (task->ssl) {
        SSL_shutdown(task->ssl);
        SSL_free(task->ssl);
        task->ssl = NULL;
    }
    if (task->client_fd >= 0) {
        close(task->client_fd);
        task->client_fd = -1;
    }
}

static ssize_t connection_recv(client_task_t *task, void *buf, size_t len) {
    if (task->ssl) {
        int rc = SSL_read(task->ssl, buf, (int)len);
        if (rc <= 0) {
            int err = SSL_get_error(task->ssl, rc);
            if (err == SSL_ERROR_ZERO_RETURN) {
                return 0;
            }
            log_error("http: SSL_read failed: %d", err);
            return -1;
        }
        return rc;
    }
    ssize_t n = recv(task->client_fd, buf, len, 0);
    return n;
}

static ssize_t connection_send(client_task_t *task, const void *buf, size_t len) {
    if (task->ssl) {
        int rc = SSL_write(task->ssl, buf, (int)len);
        if (rc <= 0) {
            int err = SSL_get_error(task->ssl, rc);
            log_error("http: SSL_write failed: %d", err);
            return -1;
        }
        return rc;
    }
    return send(task->client_fd, buf, len, 0);
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
        if (task->ssl) {
            if (SSL_accept(task->ssl) <= 0) {
                log_openssl_error("http: TLS handshake failed");
                connection_close(task);
                free(task);
                continue;
            }
        }
        int client = task->client_fd;
        SSL *ssl = task->ssl;
        free(task);
        client_task_t local = {.client_fd = client, .ssl = ssl, .next = NULL};
        handle_client(&local);
        connection_close(&local);
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
        SSL *ssl = NULL;
        if (server.cfg.http.enable_tls) {
            if (tls_ensure_context() != 0) {
                close(client);
                continue;
            }
            SSL_CTX *ctx = NULL;
            pthread_mutex_lock(&server.tls_mutex);
            ctx = server.ssl_ctx;
            if (ctx) {
                SSL_CTX_up_ref(ctx);
            }
            pthread_mutex_unlock(&server.tls_mutex);
            if (!ctx) {
                close(client);
                continue;
            }
            ssl = SSL_new(ctx);
            SSL_CTX_free(ctx);
            if (!ssl) {
                log_openssl_error("http: SSL_new failed");
                close(client);
                continue;
            }
            if (SSL_set_fd(ssl, client) != 1) {
                log_openssl_error("http: SSL_set_fd failed");
                SSL_free(ssl);
                close(client);
                continue;
            }
        }
        enqueue_client(client, ssl);
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
    if (server.cfg.http.enable_tls) {
        SSL_load_error_strings();
        OpenSSL_add_ssl_algorithms();
        if (tls_ensure_context() != 0) {
            close(server.sockfd);
            server.sockfd = -1;
            server.running = 0;
            return -1;
        }
    } else {
        pthread_mutex_lock(&server.tls_mutex);
        if (server.ssl_ctx) {
            SSL_CTX_free(server.ssl_ctx);
            server.ssl_ctx = NULL;
        }
        pthread_mutex_unlock(&server.tls_mutex);
    }
    if (server.jwt_ready) {
        key_file_deinit(&server.jwt_secret);
        server.jwt_ready = 0;
    }
    if (server.cfg.http.jwt_key_path[0]) {
        if (key_file_init(&server.jwt_secret,
                          server.cfg.http.jwt_key_path,
                          server.cfg.http.key_rotation_interval_sec) == 0) {
            server.jwt_ready = 1;
        } else {
            log_error("http: failed to initialize JWT secret manager");
        }
    }
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
    if (server.jwt_ready) {
        key_file_deinit(&server.jwt_secret);
        server.jwt_ready = 0;
    }
    pthread_mutex_lock(&server.tls_mutex);
    if (server.ssl_ctx) {
        SSL_CTX_free(server.ssl_ctx);
        server.ssl_ctx = NULL;
    }
    pthread_mutex_unlock(&server.tls_mutex);
}
