#include "blockchain.h"
#include "fkv/fkv.h"
#include "protocol/gossip.h"
#include "protocol/swarm_node.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void init_formula(Formula *formula, const char *id, const char *content, double poe) {
    memset(formula, 0, sizeof(*formula));
    strncpy(formula->id, id, sizeof(formula->id) - 1);
    strncpy(formula->content, content, sizeof(formula->content) - 1);
    formula->effectiveness = poe;
    formula->created_at = time(NULL);
    formula->representation = FORMULA_REPRESENTATION_TEXT;
}

static SwarmNode *create_node(const char *id) {
    SwarmNodeOptions opts = {0};
    strncpy(opts.node_id, id, SWARM_NODE_ID_DIGITS);
    opts.node_id[SWARM_NODE_ID_DIGITS] = '\0';
    opts.version = SWARM_PROTOCOL_VERSION;
    opts.services = 7;
    SwarmNode *node = swarm_node_create(&opts);
    assert(node);
    assert(swarm_node_start(node) == 0);
    return node;
}

static void destroy_node(SwarmNode *node) {
    if (!node) {
        return;
    }
    swarm_node_stop(node);
    swarm_node_destroy(node);
}

int main(void) {
    SwarmNode *node_a = create_node("0000000000001001");
    SwarmNode *node_b = create_node("0000000000001002");
    SwarmNode *node_c = create_node("0000000000001003");

    GossipNetwork *network = gossip_network_create();
    assert(network);
    assert(gossip_network_add_peer(network, "0000000000001001", node_a) == 0);
    assert(gossip_network_add_peer(network, "0000000000001002", node_b) == 0);
    assert(gossip_network_add_peer(network, "0000000000001003", node_c) == 0);

    SwarmFrame hello = {0};
    hello.type = SWARM_FRAME_HELLO;
    hello.payload.hello.version = SWARM_PROTOCOL_VERSION;
    memcpy(hello.payload.hello.node_id, "0000000000001001", SWARM_NODE_ID_DIGITS + 1);
    hello.payload.hello.services = 7;
    hello.payload.hello.reputation = 600;
    assert(gossip_network_broadcast(network, "0000000000001001", &hello, GOSSIP_TRANSPORT_UDP) == 0);

    SwarmPeerSnapshot snapshot = {0};
    assert(swarm_node_get_peer_snapshot(node_b, "0000000000001001", &snapshot) == 0);
    int32_t base_score = snapshot.reputation_score;
    assert(snapshot.frames[SWARM_FRAME_HELLO] == 1);

    assert(fkv_init() == 0);
    const uint8_t key1[] = {1, 2, 3};
    const uint8_t val1[] = {4, 5, 6};
    assert(fkv_put(key1, sizeof(key1), val1, sizeof(val1), FKV_ENTRY_TYPE_VALUE) == 0);
    const uint8_t key2[] = {4, 2, 0, 1};
    const uint8_t val2[] = {9, 9, 9};
    assert(fkv_put_scored(key2, sizeof(key2), val2, sizeof(val2), FKV_ENTRY_TYPE_PROGRAM, 0) == 0);

    fkv_delta_t delta = {0};
    assert(fkv_export_delta(0, &delta) == 0);
    assert(delta.count == 2);
    assert(delta.checksum == fkv_delta_compute_checksum(&delta));

    SwarmFrame delta_frame = {0};
    assert(gossip_frame_from_fkv_delta(&delta, "123456789012", &delta_frame) == 0);

    char buffer[SWARM_MAX_FRAME_SIZE + 2];
    size_t written = 0;
    assert(gossip_datagram_encode(GOSSIP_TRANSPORT_UDP, &delta_frame, buffer, sizeof(buffer), &written) == 0);
    GossipTransport decoded_transport = GOSSIP_TRANSPORT_QUIC;
    SwarmFrame parsed_frame = {0};
    assert(gossip_datagram_decode(buffer, written, &decoded_transport, &parsed_frame) == 0);
    assert(decoded_transport == GOSSIP_TRANSPORT_UDP);
    assert(parsed_frame.payload.fkv_delta.entry_count == delta_frame.payload.fkv_delta.entry_count);

    fkv_shutdown();
    assert(fkv_init() == 0);
    assert(fkv_apply_delta(&delta) == 0);
    fkv_iter_t iter = {0};
    assert(fkv_get_prefix(NULL, 0, &iter, 10) == 0);
    assert(iter.count == 2);
    fkv_iter_free(&iter);
    fkv_delta_free(&delta);
    fkv_shutdown();

    Blockchain *chain_a = blockchain_create();
    Blockchain *chain_b = blockchain_create();
    Blockchain *chain_c = blockchain_create();
    assert(chain_a && chain_b && chain_c);

    Formula high_a;
    Formula high_b;
    init_formula(&high_a, "chain-001", "payload-a", 0.94);
    init_formula(&high_b, "chain-002", "payload-b", 0.88);
    Formula *block_formulas[] = {&high_a, &high_b};
    assert(blockchain_add_block(chain_a, block_formulas, 2));
    assert(blockchain_verify(chain_a));

    Formula low;
    init_formula(&low, "chain-003", "payload-low", 0.5);
    Formula *low_block[] = {&low};
    assert(!blockchain_add_block(chain_a, low_block, 1));

    assert(blockchain_sync(chain_b, chain_a) == (int)chain_a->block_count);
    assert(blockchain_sync(chain_c, chain_a) == (int)chain_a->block_count);
    assert(blockchain_verify(chain_b));
    assert(blockchain_verify(chain_c));

    SwarmFrame block_offer = {0};
    block_offer.type = SWARM_FRAME_BLOCK_OFFER;
    memcpy(block_offer.payload.block_offer.block_id, "0000000000005555", SWARM_BLOCK_ID_DIGITS + 1);
    block_offer.payload.block_offer.height = 7;
    block_offer.payload.block_offer.poe_milli = 920;
    block_offer.payload.block_offer.program_count = 3;
    assert(gossip_network_broadcast(network, "0000000000001001", &block_offer, GOSSIP_TRANSPORT_QUIC) == 0);

    assert(swarm_node_get_peer_snapshot(node_b, "0000000000001001", &snapshot) == 0);
    assert(snapshot.blocks_accepted == 1);
    assert(snapshot.reputation_score > base_score);
    base_score = snapshot.reputation_score;

    block_offer.payload.block_offer.poe_milli = 200;
    assert(gossip_network_broadcast(network, "0000000000001001", &block_offer, GOSSIP_TRANSPORT_QUIC) == 0);
    assert(swarm_node_get_peer_snapshot(node_b, "0000000000001001", &snapshot) == 0);
    assert(snapshot.blocks_rejected == 1);
    assert(snapshot.reputation_score < base_score);

    assert(gossip_network_broadcast(network, "0000000000001001", &delta_frame, GOSSIP_TRANSPORT_UDP) == 0);
    assert(swarm_node_get_peer_snapshot(node_b, "0000000000001001", &snapshot) == 0);
    assert(snapshot.frames[SWARM_FRAME_FKV_DELTA] == 1);

    GossipTransportStats stats[GOSSIP_TRANSPORT_COUNT] = {0};
    gossip_network_get_stats(network, stats, GOSSIP_TRANSPORT_COUNT);
    assert(stats[GOSSIP_TRANSPORT_QUIC].datagrams >= 2);
    assert(stats[GOSSIP_TRANSPORT_QUIC].frames_delivered >= 2);
    assert(stats[GOSSIP_TRANSPORT_UDP].datagrams >= 1);

    blockchain_destroy(chain_a);
    blockchain_destroy(chain_b);
    blockchain_destroy(chain_c);

    gossip_network_destroy(network);

    destroy_node(node_a);
    destroy_node(node_b);
    destroy_node(node_c);

    printf("Gossip cluster synchronization test passed.\n");
    return 0;
}
