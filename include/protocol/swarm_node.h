/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_PROTOCOL_SWARM_NODE_H
#define KOLIBRI_PROTOCOL_SWARM_NODE_H

#include "protocol/swarm.h"

#include <stdint.h>

typedef struct {
    char node_id[SWARM_NODE_ID_DIGITS + 1];
    uint16_t version;
    uint16_t services;
} SwarmNodeOptions;

typedef struct {
    SwarmFrame frame;
    char peer_id[SWARM_NODE_ID_DIGITS + 1];
} SwarmOutboundFrame;

typedef struct {
    char peer_id[SWARM_NODE_ID_DIGITS + 1];
    uint32_t frames[SWARM_FRAME_TYPE_COUNT];
    int32_t reputation_score;
    uint32_t infractions;
    uint32_t successes;
    uint64_t last_seen_ms;
    SwarmHelloPayload hello;
    SwarmPingPayload ping;
    SwarmProgramOfferPayload program_offer;
    SwarmBlockOfferPayload block_offer;
    SwarmFkvDeltaPayload fkv_delta;
} SwarmPeerSnapshot;

typedef struct SwarmNode SwarmNode;

SwarmNode *swarm_node_create(const SwarmNodeOptions *opts);
int swarm_node_start(SwarmNode *node);
void swarm_node_stop(SwarmNode *node);
void swarm_node_destroy(SwarmNode *node);

SwarmAcceptDecision swarm_node_submit_frame(SwarmNode *node,
                                            const char *peer_id,
                                            const SwarmFrame *frame,
                                            int wait_for_completion);

int swarm_node_poll_outbound(SwarmNode *node,
                             SwarmOutboundFrame *out,
                             uint32_t timeout_ms);

int swarm_node_get_peer_snapshot(SwarmNode *node,
                                 const char *peer_id,
                                 SwarmPeerSnapshot *out);

#endif /* KOLIBRI_PROTOCOL_SWARM_NODE_H */
