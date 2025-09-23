#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <signal.h>

#include <assert.h>
#include <inttypes.h>
#include <sys/time.h>

#include <openssl/evp.h>
#include <openssl/core_names.h>

// Core includes
#include "kolibri_globals.h"
#include "kolibri_rules.h"
#include "kolibri_decimal_cell.h"
#include "kolibri_proto.h"
#include "kolibri_ping.h"
#include "kolibri_knowledge.h"
#include "kolibri_rule_stats.h"
#include "kolibri_ai.h"
#include "kolibri_log.h"

static pthread_mutex_t socket_mutex = PTHREAD_MUTEX_INITIALIZER;

// Таймауты для сокетов (в секундах)
#define SOCKET_RECV_TIMEOUT 5
#define SOCKET_SEND_TIMEOUT 5

// Функция очистки ресурсов
static void cleanup_resources(void) {
    pthread_mutex_lock(&socket_mutex);
    if (server_sock >= 0) {
        close(server_sock);
        server_sock = -1;
    }
    pthread_mutex_unlock(&socket_mutex);
    
    // Очистка правил и других ресурсов
    cleanup_rules(&rules);
    cleanup_decimal_cell(&cell);
}

// Флаг для graceful shutdown
static volatile sig_atomic_t keep_running = 1;

// Обработчик сигналов для graceful shutdown
static void sig_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        printf("\nReceived signal %d, shutting down...\n", signo);
        keep_running = 0;
    }
}


// Прототипы функций
void cleanup_decimal_cell(decimal_cell_t* cell);

// ==========================
// Configuration and Constants

#define MAGIC_BYTES "KLB1"
#define MAGIC_LEN 4
#define BUFFER_SIZE 8192
#define DEFAULT_PORT 9000

#define MSG_HELLO 20
#define MSG_ACK   21

// ==========================
// Global state



static rule_stats_t rule_stats[MAX_RULES];


static uint64_t eval_count = 0;
static uint64_t success_count = 0;

static bool pilot_mode = false;

// Mutexes for thread safety
static pthread_mutex_t rules_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

// Отправка HELLO соседу
void send_hello_to_neighbor(struct sockaddr_in* addr) {
    char msg[BUFFER_SIZE];
    memset(msg, 0, sizeof(msg));
    memcpy(msg, MAGIC_BYTES, MAGIC_LEN);
    msg[MAGIC_LEN] = MSG_HELLO;
    msg[MAGIC_LEN+1] = cell.node_digit;
    sendto(server_sock, msg, MAGIC_LEN+2, 0, (struct sockaddr*)addr, sizeof(*addr));
}

// Псевдо-адрес соседа (для теста: порт = базовый + цифра)
void fill_neighbor_addr(uint8_t digit, struct sockaddr_in* addr, uint16_t base_port) {
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons(base_port + digit);
    addr->sin_addr.s_addr = htonl(0x7f000001); // 127.0.0.1
}

// ==========================
// Utilities

#define MAX_NEIGHBORS 9

typedef struct {
    char node_id[32];
    struct sockaddr_in addr;
} neighbor_t;

// AI подсистема
static KolibriAI* node_ai = NULL;

uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* Локальная функция add_neighbor удалена, используем add_neighbor(&cell, digit) из kolibri_decimal_cell.h */

// ==========================
// Network handling

static int init_server(uint16_t port) {
    struct sockaddr_in addr = {0};
    int ret = -1;
    
    pthread_mutex_lock(&socket_mutex);
    
    server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        goto cleanup;
    }
    
    // Enable address reuse
    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Failed to set SO_REUSEADDR: %s", strerror(errno));
        goto cleanup;
    }
    
    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = SOCKET_RECV_TIMEOUT;
    tv.tv_usec = 0;
    if (setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        LOG_ERROR("Failed to set SO_RCVTIMEO: %s", strerror(errno));
        goto cleanup;
    }
    
    // Set send timeout
    tv.tv_sec = SOCKET_SEND_TIMEOUT;
    if (setsockopt(server_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        LOG_ERROR("Failed to set SO_SNDTIMEO: %s", strerror(errno));
        goto cleanup;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind to port %d: %s", port, strerror(errno));
        goto cleanup;
    }
    
    LOG_SUCCESS("Server started on port %d", port);
    ret = 0; // Success
    
cleanup:
    if (ret < 0 && server_sock >= 0) {
        close(server_sock);
        server_sock = -1;
    }
    pthread_mutex_unlock(&socket_mutex);
    return ret;
}

static void process_message(const char* data, size_t len, struct sockaddr_in* src_addr) {
    if (len < MAGIC_LEN + 1) return;
    
    if (memcmp(data, MAGIC_BYTES, MAGIC_LEN) != 0) {
        printf("Invalid magic bytes\n");
        return;
    }
    
    uint8_t type = data[MAGIC_LEN];
    char response[BUFFER_SIZE];
    size_t response_len = 0;
    switch (type) {
        case MSG_HELLO: {
            // Ответить ACK
            response[MAGIC_LEN] = MSG_ACK;
            memcpy(response, MAGIC_BYTES, MAGIC_LEN);
            response[MAGIC_LEN+1] = cell.node_digit;
            response_len = MAGIC_LEN + 2;
            sendto(server_sock, response, response_len, 0, (struct sockaddr*)src_addr, sizeof(*src_addr));
            // Пример: считаем успешное применение первого правила (для демонстрации)
            pthread_mutex_lock(&rules_mutex);
            if (rules.count > 0) {
                pthread_mutex_lock(&stats_mutex);
                update_rule_stats(&rule_stats[0], true, 1.0); // success, response_time=1.0
                adjust_fitness(&rules.fitness[0], &rule_stats[0]);
                pthread_mutex_unlock(&stats_mutex);
            }
            pthread_mutex_unlock(&rules_mutex);
            break;
        }
        case MSG_ACK: {
            uint8_t from_digit = (len > MAGIC_LEN+1) ? (uint8_t)data[MAGIC_LEN+1] : 255;
            printf("[DEBUG] Received ACK from node %d\n", from_digit);
            break;
        }
        case 42: { // MSG_MIGRATE_RULE
            // Получение правила от соседа
            char pattern[MAX_PATTERN_LEN], action[MAX_ACTION_LEN];
            int tier = 0; double fitness = 0.0;
            sscanf((char*)data+MAGIC_LEN+1, "%255[^|]|%255[^|]|%d|%lf", pattern, action, &tier, &fitness);
            // Проверяем уникальность
            int unique = 1;
            for (int i = 0; i < rules.count; i++) {
                if (strcmp(rules.patterns[i], pattern) == 0 && strcmp(rules.actions[i], action) == 0) {
                    unique = 0; break;
                }
            }
            if (unique && rules.count < MAX_RULES) {
                add_rule(&rules, pattern, action, tier, fitness);
                printf("[MIGRATE] Accepted migrated rule: %s -> %s\n", pattern, action);
            }
            break;
        }
        default:
            printf("Unknown message type: %d\n", type);
            break;
    }
}

// Создание метаправила на основе двух лучших по fitness
void create_metarule(void) {
    if (rules.count < 2) return;
    int best1 = 0, best2 = 1;
    for (int i = 1; i < rules.count; i++) {
        if (rules.fitness[i] > rules.fitness[best1]) {
            best2 = best1;
            best1 = i;
        } else if (rules.fitness[i] > rules.fitness[best2] && i != best1) {
            best2 = i;
        }
    }
    char new_pattern[MAX_PATTERN_LEN];
    char new_action[MAX_ACTION_LEN];
    snprintf(new_pattern, sizeof(new_pattern), "%s_%s", rules.patterns[best1], rules.patterns[best2]);
    snprintf(new_action, sizeof(new_action), "%s_%s", rules.actions[best1], rules.actions[best2]);
    int new_tier = (rules.tiers[best1] > rules.tiers[best2] ? rules.tiers[best1] : rules.tiers[best2]) + 1;
    double new_fitness = (rules.fitness[best1] + rules.fitness[best2]) / 2.0;
    if (rules.count < MAX_RULES) {
        add_rule(&rules, new_pattern, new_action, new_tier, new_fitness);
        printf("[META] Created metarule: %s -> %s\n", new_pattern, new_action);
    }
}

// Замена неактивных соседей на новые
void adapt_neighbors(void) {
    uint64_t now = now_ms();
    for (int i = 0; i < cell.n_neighbors; i++) {
        if (!cell.is_active[i] && (now - cell.last_sync[i] > 90000)) {
            // Найти новую цифру, не совпадающую с текущими
            uint8_t used[10] = {0};
            used[cell.node_digit] = 1;
            for (int j = 0; j < cell.n_neighbors; j++) used[cell.neighbor_digits[j]] = 1;
            for (uint8_t d = 0; d < 10; d++) {
                if (!used[d]) {
                    cell.neighbor_digits[i] = d;
                    cell.last_sync[i] = now;
                    cell.is_active[i] = true;
                    printf("[TOPOLOGY] Neighbor %d replaced by digit %d\n", i, d);
                    break;
                }
            }
        }
    }
}

// Отправка лучшего правила соседу
void migrate_best_rule(void) {
    if (rules.count == 0 || cell.n_neighbors == 0) return;
    // Находим правило с максимальным fitness
    int best = 0;
    for (int i = 1; i < rules.count; i++) {
        if (rules.fitness[i] > rules.fitness[best]) best = i;
    }
    // Выбираем случайного соседа
    int nidx = rand() % cell.n_neighbors;
    struct sockaddr_in n_addr;
    fill_neighbor_addr(cell.neighbor_digits[nidx], &n_addr, DEFAULT_PORT);
    // Формируем сообщение (MAGIC + тип + pattern + action + tier + fitness)
    char msg[BUFFER_SIZE];
    memset(msg, 0, sizeof(msg));
    memcpy(msg, MAGIC_BYTES, MAGIC_LEN);
    msg[MAGIC_LEN] = 42; // MSG_MIGRATE_RULE
    int off = MAGIC_LEN + 1;
    int plen = snprintf(msg+off, sizeof(msg)-off, "%s|%s|%d|%.4f", rules.patterns[best], rules.actions[best], rules.tiers[best], rules.fitness[best]);
    sendto(server_sock, msg, off+plen, 0, (struct sockaddr*)&n_addr, sizeof(n_addr));
    printf("[MIGRATE] Sent best rule to neighbor %d: %s -> %s\n", cell.neighbor_digits[nidx], rules.patterns[best], rules.actions[best]);
}

// ==========================
// Main loop

static void run_server(void) {
    char buffer[BUFFER_SIZE];
    struct sockaddr_in src_addr;
    socklen_t src_len = sizeof(src_addr);
    uint64_t last_sync = 0;
    static uint64_t last_log = 0;
    uint64_t last_meta = 0;
    uint64_t last_migrate = 0;
    while (1) {
        ssize_t len = recvfrom(server_sock, buffer, sizeof(buffer), 0,
                              (struct sockaddr*)&src_addr, &src_len);
        if (len < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            break;
        }
        process_message(buffer, len, &src_addr);
        uint64_t t = now_ms();
        // Периодическая синхронизация с соседями (HELLO)
        if (t - last_sync > 1000) {
            for (int i = 0; i < cell.n_neighbors; i++) {
                struct sockaddr_in n_addr;
                fill_neighbor_addr(cell.neighbor_digits[i], &n_addr, DEFAULT_PORT);
                send_hello_to_neighbor(&n_addr);
            }
            last_sync = t;
        }
        // Периодическое создание метаправил (раз в 30 сек)
        if (t - last_meta > 30000) {
            create_metarule();
            last_meta = t;
        }
        // Периодическая адаптация топологии (раз в 60 сек)
        if (t % 60000 < 1000) {
            adapt_neighbors();
        }
        // Периодическая миграция лучшего правила (раз в 5 минут)
        if (now_ms() - last_migrate > 300000) {
            migrate_best_rule();
            last_migrate = now_ms();
        }
        
        // Периодическое логирование статистики (каждые 10 секунд)
        if (t - last_log > 10000) {
            for (int i = 0; i < rules.count; i++) {
                printf("T=%" PRIu64 " RULE=%d pattern=%s action=%s tier=%d fitness=%.4f\n",
                       (uint64_t)t, i, rules.patterns[i], rules.actions[i],
                       rules.tiers[i], rules.fitness[i]);
            }
            for (int i = 0; i < cell.n_neighbors; i++) {
                printf("T=%" PRIu64 " NEIGHBOR=%d digit=%d active=%d last_sync=%" PRIu64 "\n",
                       (uint64_t)t, i, cell.neighbor_digits[i],
                       cell.neighbor_digits[i] != 255, (uint64_t)cell.last_sync[i]);
            }
            last_log = t;
        }
        // Периодическое логирование статистики (каждые 10 секунд)
        // (удалено дублирующее логирование)
        
        if (pilot_mode) {
            double p0 = (eval_count > 0) ? (double)success_count / eval_count : 0.0;
            printf("[PILOT DEBUG] eval_count=%" PRIu64 ", success_count=%" PRIu64 ", p0=%.4f\n",
                   (uint64_t)eval_count, (uint64_t)success_count, p0);
            if (eval_count == 0) {
                printf("[PILOT DEBUG] пока нет выполненных правил, но пилотный режим активен\n");
            }
            last_log = t;
        }
    }
}

// ==========================
// Entry point

extern void run_http_status_server(int port, rules_t* rules, decimal_cell_t* cell);

int main(int argc, char** argv) {
    uint16_t port = DEFAULT_PORT;
    int digit_arg = -1;

    // Парсим аргументы командной строки
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--pilot_mode") == 0) {
            pilot_mode = true;
        }
        if (strcmp(argv[i], "--digit") == 0 && i + 1 < argc) {
            digit_arg = atoi(argv[i + 1]);
            if (digit_arg < 0 || digit_arg > 9) digit_arg = -1;
            i++;
        }
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[i + 1]);
            i++;
        }
    }

    // Устанавливаем обработчики сигналов
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    // Initialize random seed
    srand(time(NULL));

    // Initialize rules system
    init_rules(&rules);

    // Load base knowledge
    const char* specializations[] = {"math", "text", "logic", "memory"};
    size_t spec_count = sizeof(specializations) / sizeof(specializations[0]);

    // Randomly select a specialization
    const char* selected_spec = specializations[rand() % spec_count];
    printf("Selected specialization: %s\n", selected_spec);

    if (load_base_knowledge("rules/base_knowledge.json", selected_spec, &rules) != 0) {
        fprintf(stderr, "Failed to load base knowledge\n");
        return 1;
    }

    // Инициализация сети и подсистем
    init_ping_stats();
    
    // Инициализация десятичной ячейки с цифрой из аргумента или случайной
    uint8_t my_digit = (digit_arg >= 0) ? (uint8_t)digit_arg : (rand() % 10);
    init_decimal_cell(&cell, my_digit);
    LOG_INFO("Node digit: %d", my_digit);
    
    // Инициализация сервера
    if (init_server(port) < 0) {
        LOG_ERROR("Failed to initialize server");
        cleanup_resources();
        return 1;
    }

    // Добавляем 9 соседей (все цифры кроме своей)
    for (uint8_t d = 0; d < 10; d++) {
        if (d == my_digit) continue;
        add_neighbor(&cell, d);
    }

    // Инициализация AI подсистемы
    node_ai = kolibri_ai_create();
    if (!node_ai) {
        fprintf(stderr, "Failed to initialize AI subsystem\n");
        return 1;
    }
    kolibri_ai_start(node_ai);

    // Запуск HTTP API для мониторинга (мониторинговый порт)
    int http_port = port + 10000;
    run_http_status_server(http_port, &rules, &cell);

    // Основной цикл обработки событий
    run_server();

    // Очистка ресурсов (эта часть не будет достигнута если run_server не завершится, но оставим для полноты)
    if (server_sock >= 0) {
        close(server_sock);
        server_sock = -1;
    }
    if (node_ai) {
        kolibri_ai_stop(node_ai);
        kolibri_ai_destroy(node_ai);
        node_ai = NULL;
    }
    cleanup_rules(&rules);
    printf("Node shutdown complete\n");
    return 0;
}
