#include "network.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define NETWORK_TIMEOUT_SECONDS 5
#define NETWORK_MAX_MESSAGE_SIZE (64 * 1024)

static int listen_fd = -1;

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return false;
    }
    return true;
}

static bool apply_timeouts(int fd) {
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        return false;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        return false;
    }
    return true;
}

static bool send_all(int fd, const void* buffer, size_t length) {
    const char* ptr = (const char*)buffer;
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t sent = send(fd, ptr + total_sent, length - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total_sent += (size_t)sent;
    }
    return true;
}

static bool recv_all(int fd, void* buffer, size_t length) {
    char* ptr = (char*)buffer;
    size_t total_received = 0;
    while (total_received < length) {
        ssize_t received = recv(fd, ptr + total_received, length - total_received, 0);
        if (received == 0) {
            return false; // соединение закрыто раньше времени
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total_received += (size_t)received;
    }
    return true;
}

bool network_init(int port) {
    if (listen_fd != -1) {
        return true;
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return false;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    if (!set_nonblocking(listen_fd)) {
        perror("fcntl");
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    if (listen(listen_fd, 8) < 0) {
        perror("listen");
        close(listen_fd);
        listen_fd = -1;
        return false;
    }

    fprintf(stdout, "[NETWORK] Listening on port %d\n", port);
    return true;
}

bool network_send_data(const char* host, int port, const char* data) {
    if (!host || !data) {
        fprintf(stderr, "[NETWORK] Invalid arguments for send\n");
        return false;
    }

    size_t payload_len = strlen(data);
    if (payload_len == 0 || payload_len > NETWORK_MAX_MESSAGE_SIZE) {
        fprintf(stderr, "[NETWORK] Payload size %zu is invalid\n", payload_len);
        return false;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return false;
    }

    if (!apply_timeouts(sock)) {
        perror("setsockopt");
        close(sock);
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        fprintf(stderr, "[NETWORK] Invalid host address: %s\n", host);
        close(sock);
        return false;
    }

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return false;
    }

    uint32_t len_net = htonl((uint32_t)payload_len);
    if (!send_all(sock, &len_net, sizeof(len_net))) {
        perror("send length");
        close(sock);
        return false;
    }

    if (!send_all(sock, data, payload_len)) {
        perror("send payload");
        close(sock);
        return false;
    }

    char ack_buffer[32];
    memset(ack_buffer, 0, sizeof(ack_buffer));
    ssize_t received = recv(sock, ack_buffer, sizeof(ack_buffer) - 1, 0);
    if (received <= 0) {
        perror("recv ack");
        close(sock);
        return false;
    }

    close(sock);

    if (strstr(ack_buffer, "ok") == NULL && strstr(ack_buffer, "OK") == NULL) {
        fprintf(stderr, "[NETWORK] Unexpected ACK: %s\n", ack_buffer);
        return false;
    }

    fprintf(stdout, "[NETWORK] Delivery confirmed: %s\n", ack_buffer);
    return true;
}

char* network_receive_data(void) {
    if (listen_fd < 0) {
        return NULL;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(listen_fd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    int ready = select(listen_fd + 1, &readfds, NULL, NULL, &timeout);
    if (ready <= 0) {
        return NULL;
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("accept");
        return NULL;
    }

    if (!apply_timeouts(client_fd)) {
        perror("setsockopt");
        close(client_fd);
        return NULL;
    }

    uint32_t len_net = 0;
    if (!recv_all(client_fd, &len_net, sizeof(len_net))) {
        fprintf(stderr, "[NETWORK] Failed to read payload length\n");
        close(client_fd);
        return NULL;
    }

    uint32_t payload_len = ntohl(len_net);
    if (payload_len == 0 || payload_len > NETWORK_MAX_MESSAGE_SIZE) {
        fprintf(stderr, "[NETWORK] Payload length %u invalid\n", payload_len);
        close(client_fd);
        return NULL;
    }

    char* buffer = (char*)malloc(payload_len + 1);
    if (!buffer) {
        fprintf(stderr, "[NETWORK] Memory allocation failed\n");
        close(client_fd);
        return NULL;
    }

    if (!recv_all(client_fd, buffer, payload_len)) {
        fprintf(stderr, "[NETWORK] Failed to read payload body\n");
        free(buffer);
        close(client_fd);
        return NULL;
    }

    buffer[payload_len] = '\0';

    const char* ack = "{\"status\":\"ok\"}";
    if (!send_all(client_fd, ack, strlen(ack))) {
        fprintf(stderr, "[NETWORK] Failed to send ACK\n");
        free(buffer);
        close(client_fd);
        return NULL;
    }

    close(client_fd);

    fprintf(stdout, "[NETWORK] Received %u bytes: %s\n", payload_len, buffer);
    return buffer;
}

void network_cleanup(void) {
    if (listen_fd != -1) {
        close(listen_fd);
        listen_fd = -1;
    }
}
