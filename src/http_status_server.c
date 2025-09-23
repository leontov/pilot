#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "kolibri_rules.h"
#include "kolibri_decimal_cell.h"

// Минимальный HTTP сервер для мониторинга
void run_http_status_server(int port, rules_t* rules, decimal_cell_t* cell) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(sock, 5);
    while (1) {
        int client = accept(sock, NULL, NULL);
        if (client < 0) continue;
        char req[1024] = {0};
        read(client, req, sizeof(req)-1);
        // Простой парсер GET /status
        if (strstr(req, "GET /status")) {
            char resp[4096];
            size_t neighbors = decimal_cell_child_count(cell, false);
            snprintf(resp, sizeof(resp),
                "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nDigit: %d\nNeighbors: %zu\nRules: %d\n",
                cell->node_digit, neighbors, rules->count);
            write(client, resp, strlen(resp));
        } else if (strstr(req, "GET /rules")) {
            char resp[4096];
            int off = snprintf(resp, sizeof(resp), "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
            for (size_t i = 0; i < rules->count && off < sizeof(resp)-128; i++) {
                off += snprintf(resp+off, sizeof(resp)-off, "Rule %zu: %s -> %s, tier=%d, fitness=%.3f\n",
                    i, rules->patterns[i], rules->actions[i], rules->tiers[i], rules->fitness[i]);
            }
            write(client, resp, strlen(resp));
        } else if (strstr(req, "GET /neighbors")) {
            char resp[4096];
            int off = snprintf(resp, sizeof(resp), "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n");
            uint8_t digits[DECIMAL_BRANCHING];
            size_t count = decimal_cell_collect_children(cell, digits, DECIMAL_BRANCHING, false);
            for (size_t i = 0; i < count && off < sizeof(resp)-128; i++) {
                uint8_t digit = digits[i];
                off += snprintf(resp+off, sizeof(resp)-off, "Neighbor %zu: digit=%u active=%d last_sync=%llu\n",
                    i, digit, decimal_cell_child_is_active(cell, digit),
                    (unsigned long long)decimal_cell_child_last_sync(cell, digit));
            }
            write(client, resp, strlen(resp));
        } else {
            char resp[] = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot found\n";
            write(client, resp, strlen(resp));
        }
        close(client);
    }
}
