/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_UTIL_CONFIG_H
#define KOLIBRI_UTIL_CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "synthesis/search.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char snapshot_path[256];
    uint32_t snapshot_limit;
} ai_persistence_config_t;

typedef struct {
    char host[64];
    uint16_t port;
    uint32_t max_body_size;
    bool enable_tls;
    bool require_client_auth;
    uint32_t key_rotation_interval_sec;
    char tls_cert_path[256];
    char tls_key_path[256];
    char tls_client_ca_path[256];
    char jwt_issuer[128];
    char jwt_audience[128];
    char jwt_key_path[256];
} http_config_t;

typedef struct {
    uint32_t max_steps;
    uint32_t max_stack;
    uint32_t trace_depth;
} vm_config_t;

typedef struct {
    uint32_t tasks_per_iteration;
    uint32_t max_difficulty;
} KolibriAISelfplayConfig;

typedef struct {
    http_config_t http;
    vm_config_t vm;
    FormulaSearchConfig search;
    uint32_t seed;
    ai_persistence_config_t ai;
    KolibriAISelfplayConfig selfplay;
#define SECURITY_SIGNER_ID_DIGITS 16
    struct {
        char node_private_key_path[256];
        char node_public_key_path[256];
        char swarm_trust_store[256];
        uint32_t rotation_interval_sec;
        char signer_id[SECURITY_SIGNER_ID_DIGITS + 1];
    } security;
} kolibri_config_t;

int config_load(const char *path, kolibri_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_UTIL_CONFIG_H */
