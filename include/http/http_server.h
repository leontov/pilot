/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_HTTP_SERVER_H
#define KOLIBRI_HTTP_SERVER_H

#include <stdint.h>

#include "util/config.h"
#include "vm/vm.h"

int http_server_start(const kolibri_config_t *cfg, vm_scheduler_t *scheduler);
void http_server_stop(void);

#endif
