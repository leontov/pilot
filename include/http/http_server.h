/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_HTTP_SERVER_H
#define KOLIBRI_HTTP_SERVER_H

#include <stdint.h>

#include "util/config.h"

int http_server_start(const kolibri_config_t *cfg);
void http_server_stop(void);

#endif
