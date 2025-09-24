#include "protocol/swarm.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_hello_roundtrip(void) {
    SwarmFrame frame = {0};
    frame.type = SWARM_FRAME_HELLO;
    frame.payload.hello.version = 2;
    memcpy(frame.payload.hello.node_id, "0000000000004242", SWARM_NODE_ID_DIGITS + 1);
    frame.payload.hello.services = 42;
    frame.payload.hello.reputation = 620;

    char encoded[SWARM_MAX_FRAME_SIZE];
    size_t written = 0;
    assert(swarm_frame_serialize(&frame, encoded, sizeof(encoded), &written) == 0);
    assert(written > 0);
    assert(written == strlen(encoded));

    SwarmFrame parsed = {0};
    assert(swarm_frame_parse(encoded, written, &parsed) == 0);
    assert(parsed.type == SWARM_FRAME_HELLO);
    assert(parsed.payload.hello.version == frame.payload.hello.version);
    assert(strcmp(parsed.payload.hello.node_id, frame.payload.hello.node_id) == 0);
    assert(parsed.payload.hello.services == frame.payload.hello.services);
    assert(parsed.payload.hello.reputation == frame.payload.hello.reputation);
}

static void test_rate_limiting_and_reputation(void) {
    SwarmPeerState peer;
    swarm_peer_state_init(&peer, 0);

    // Burst allows three pings immediately
    for (int i = 0; i < 3; ++i) {
        assert(swarm_peer_should_accept(&peer, SWARM_FRAME_PING, 0) == SWARM_DECISION_ACCEPT);
        swarm_peer_report_success(&peer, SWARM_FRAME_PING);
    }

    // Subsequent ping without waiting should be rate limited
    assert(swarm_peer_should_accept(&peer, SWARM_FRAME_PING, 0) == SWARM_DECISION_RATE_LIMITED);
    int32_t score_after_limit = peer.reputation.score;
    assert(score_after_limit < 600);

    // After some time tokens refill and peer can send again
    assert(swarm_peer_should_accept(&peer, SWARM_FRAME_PING, 5000) == SWARM_DECISION_ACCEPT);
}

static void test_reputation_blocking(void) {
    SwarmPeerState peer;
    swarm_peer_state_init(&peer, 0);

    for (int i = 0; i < 10; ++i) {
        swarm_peer_report_violation(&peer, SWARM_FRAME_PROGRAM_OFFER);
    }
    assert(strcmp(swarm_reputation_class(&peer.reputation), "blocked") == 0);
    assert(swarm_peer_should_accept(&peer, SWARM_FRAME_PROGRAM_OFFER, 1000) == SWARM_DECISION_REPUTATION_BLOCKED);
}

static void test_fkv_delta_roundtrip(void) {
    SwarmFrame frame = {0};
    frame.type = SWARM_FRAME_FKV_DELTA;
    memcpy(frame.payload.fkv_delta.prefix, "123456789012", SWARM_PREFIX_DIGITS + 1);
    frame.payload.fkv_delta.entry_count = 12;
    frame.payload.fkv_delta.compressed_size = 4096;
    frame.payload.fkv_delta.checksum = 1234;

    char encoded[SWARM_MAX_FRAME_SIZE];
    size_t written = 0;
    assert(swarm_frame_serialize(&frame, encoded, sizeof(encoded), &written) == 0);
    SwarmFrame parsed = {0};
    assert(swarm_frame_parse(encoded, written, &parsed) == 0);
    assert(parsed.type == SWARM_FRAME_FKV_DELTA);
    assert(strcmp(parsed.payload.fkv_delta.prefix, frame.payload.fkv_delta.prefix) == 0);
    assert(parsed.payload.fkv_delta.entry_count == frame.payload.fkv_delta.entry_count);
    assert(parsed.payload.fkv_delta.compressed_size == frame.payload.fkv_delta.compressed_size);
    assert(parsed.payload.fkv_delta.checksum == frame.payload.fkv_delta.checksum);
}

int main(void) {
    test_hello_roundtrip();
    test_rate_limiting_and_reputation();
    test_reputation_blocking();
    test_fkv_delta_roundtrip();
    printf("swarm protocol tests passed\n");
    return 0;
}
