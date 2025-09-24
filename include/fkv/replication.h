#ifndef KOLIBRI_FKV_REPLICATION_H
#define KOLIBRI_FKV_REPLICATION_H

#include "protocol/swarm.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int fkv_replication_build_delta(const uint8_t *prefix_digits,
                                size_t prefix_len,
                                SwarmFrame *frame);
int fkv_replication_apply_delta(const SwarmFrame *frame);
void fkv_replication_free_delta(SwarmFrame *frame);

#ifdef __cplusplus
}
#endif

#endif
