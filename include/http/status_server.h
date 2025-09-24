/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_HTTP_STATUS_SERVER_H
#define KOLIBRI_HTTP_STATUS_SERVER_H

#include <signal.h>
#include <stdint.h>

#include "kolibri_ai.h"

#ifdef __cplusplus
extern "C" {
#endif

int http_status_server_init(uint16_t port,
                            volatile sig_atomic_t *keep_running,
                            KolibriAI *ai);
void http_status_server_run(void);
void http_status_server_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_HTTP_STATUS_SERVER_H */
