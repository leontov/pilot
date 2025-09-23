#include "blockchain.h"
#include "fkv/fkv.h"
#include "http/http_routes.h"
#include "http/http_server.h"
#include "util/config.h"
#include "util/log.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

static int run_bench(void) {
    log_info("Benchmarks are not implemented yet");
    return 0;
}

int main(int argc, char **argv) {
    log_set_level(LOG_LEVEL_INFO);
    FILE *log_fp = fopen("logs/kolibri.log", "a");
    if (log_fp) {
        log_set_file(log_fp);
    }

    kolibri_config_t cfg;
    if (config_load("cfg/kolibri.jsonc", &cfg) != 0) {
        log_warn("could not read cfg/kolibri.jsonc, using defaults");
    }

    if (argc > 1 && strcmp(argv[1], "--bench") == 0) {
        return run_bench();
    }

    if (fkv_init() != 0) {
        log_error("failed to initialize F-KV");
        return 1;
    }

    Blockchain *chain = blockchain_create();
    if (!chain) {
        log_error("failed to initialize blockchain");
        fkv_shutdown();
        if (log_fp) {
            fclose(log_fp);
        }
        return 1;
    }
    http_routes_set_blockchain(chain);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (http_server_start(&cfg) != 0) {
        log_error("failed to start HTTP server");
        http_routes_set_blockchain(NULL);
        blockchain_destroy(chain);
        fkv_shutdown();
        if (log_fp) {
            fclose(log_fp);
        }
        return 1;
    }

    while (running) {
        pause();
    }

    http_server_stop();
    http_routes_set_blockchain(NULL);
    blockchain_destroy(chain);
    fkv_shutdown();
    if (log_fp) {
        fclose(log_fp);
    }
    return 0;
}
