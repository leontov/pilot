#include "protocol/swarm_node.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fill_hello_frame(SwarmFrame *frame) {
    memset(frame, 0, sizeof(*frame));
    frame->type = SWARM_FRAME_HELLO;
    frame->payload.hello.version = 2;
    memcpy(frame->payload.hello.node_id, "0000000000001234", SWARM_NODE_ID_DIGITS + 1);
    frame->payload.hello.services = 3;
    frame->payload.hello.reputation = 620;
}

static void fill_ping_frame(SwarmFrame *frame, uint32_t nonce, uint32_t latency) {
    memset(frame, 0, sizeof(*frame));
    frame->type = SWARM_FRAME_PING;
    frame->payload.ping.nonce = nonce;
    frame->payload.ping.latency_hint_ms = latency;
}

static void fill_program_offer(SwarmFrame *frame) {
    memset(frame, 0, sizeof(*frame));
    frame->type = SWARM_FRAME_PROGRAM_OFFER;
    memcpy(frame->payload.program_offer.program_id, "0000000000009001", SWARM_PROGRAM_ID_DIGITS + 1);
    frame->payload.program_offer.poe_milli = 950;
    frame->payload.program_offer.mdl_score = 1200;
    frame->payload.program_offer.gas_used = 4200;
}

static void fill_block_offer(SwarmFrame *frame) {
    memset(frame, 0, sizeof(*frame));
    frame->type = SWARM_FRAME_BLOCK_OFFER;
    memcpy(frame->payload.block_offer.block_id, "0000000000004321", SWARM_BLOCK_ID_DIGITS + 1);
    frame->payload.block_offer.height = 42;
    frame->payload.block_offer.poe_milli = 870;
    frame->payload.block_offer.program_count = 8;
}

static void fill_fkv_delta(SwarmFrame *frame) {
    memset(frame, 0, sizeof(*frame));
    frame->type = SWARM_FRAME_FKV_DELTA;
    memcpy(frame->payload.fkv_delta.prefix, "987654321000", SWARM_PREFIX_DIGITS + 1);
    frame->payload.fkv_delta.entry_count = 5;
    frame->payload.fkv_delta.compressed_size = 2048;
    frame->payload.fkv_delta.checksum = 12345 % 100000;
}

int main(void) {
    SwarmNodeOptions opts = {0};
    memcpy(opts.node_id, "0000000000009999", SWARM_NODE_ID_DIGITS + 1);
    opts.version = SWARM_PROTOCOL_VERSION;
    opts.services = 7;

    SwarmNode *node = swarm_node_create(&opts);
    assert(node != NULL);
    assert(swarm_node_start(node) == 0);

    const char *peer_id = "0000000000001234";

    SwarmFrame frame;
    fill_hello_frame(&frame);
    assert(swarm_node_submit_frame(node, peer_id, &frame, 1) == SWARM_DECISION_ACCEPT);

    SwarmPeerSnapshot snapshot;
    assert(swarm_node_get_peer_snapshot(node, peer_id, &snapshot) == 0);
    assert(snapshot.frames[SWARM_FRAME_HELLO] == 1);
    assert(snapshot.hello.version == 2);
    assert(strcmp(snapshot.hello.node_id, peer_id) == 0);

    SwarmOutboundFrame outbound;
    assert(swarm_node_poll_outbound(node, &outbound, 500) == 0);
    assert(outbound.frame.type == SWARM_FRAME_HELLO);
    assert(strcmp(outbound.frame.payload.hello.node_id, opts.node_id) == 0);

    fill_ping_frame(&frame, 777, 33);
    assert(swarm_node_submit_frame(node, peer_id, &frame, 1) == SWARM_DECISION_ACCEPT);
    assert(swarm_node_poll_outbound(node, &outbound, 500) == 0);
    assert(outbound.frame.type == SWARM_FRAME_PING);
    assert(outbound.frame.payload.ping.nonce == 777);

    fill_program_offer(&frame);
    assert(swarm_node_submit_frame(node, peer_id, &frame, 1) == SWARM_DECISION_ACCEPT);
    fill_block_offer(&frame);
    assert(swarm_node_submit_frame(node, peer_id, &frame, 1) == SWARM_DECISION_ACCEPT);
    fill_fkv_delta(&frame);
    assert(swarm_node_submit_frame(node, peer_id, &frame, 1) == SWARM_DECISION_ACCEPT);

    assert(swarm_node_get_peer_snapshot(node, peer_id, &snapshot) == 0);
    assert(snapshot.frames[SWARM_FRAME_PROGRAM_OFFER] == 1);
    assert(strcmp(snapshot.program_offer.program_id, "0000000000009001") == 0);
    assert(snapshot.block_offer.height == 42);
    assert(snapshot.fkv_delta.entry_count == 5);

    fill_ping_frame(&frame, 800, 10);
    assert(swarm_node_submit_frame(node, peer_id, &frame, 1) == SWARM_DECISION_ACCEPT);
    fill_ping_frame(&frame, 801, 10);
    assert(swarm_node_submit_frame(node, peer_id, &frame, 1) == SWARM_DECISION_ACCEPT);

    assert(swarm_node_get_peer_snapshot(node, peer_id, &snapshot) == 0);
    int32_t score_before_limit = snapshot.reputation_score;

    fill_ping_frame(&frame, 802, 10);
    SwarmAcceptDecision decision = swarm_node_submit_frame(node, peer_id, &frame, 1);
    assert(decision == SWARM_DECISION_RATE_LIMITED);

    assert(swarm_node_get_peer_snapshot(node, peer_id, &snapshot) == 0);
    assert(snapshot.reputation_score < score_before_limit);

    swarm_node_stop(node);
    swarm_node_destroy(node);

    printf("swarm exchange tests passed\n");
    return 0;
}
