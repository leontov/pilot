/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_HTTP_ROUTES_H
#define KOLIBRI_HTTP_ROUTES_H

#include <stddef.h>
#include <stdint.h>

#include "blockchain.h"
#include "util/config.h"

struct KolibriAI;

typedef struct {
    char *data;
    size_t len;
    int status;
    char content_type[64];
} http_response_t;

int http_handle_request(const kolibri_config_t *cfg,
                        const char *method,
                        const char *path,
                        const char *body,
                        size_t body_len,
                        http_response_t *resp);
void http_response_free(http_response_t *resp);
void http_routes_set_start_time(uint64_t ms_since_epoch);
void http_routes_set_blockchain(Blockchain *chain);
void http_routes_set_ai(struct KolibriAI *ai);


#endif
