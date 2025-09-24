#include "protocol/gossip.h"

#include "util/log.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

struct GossipPeer {
    char node_id[SWARM_NODE_ID_DIGITS + 1];
    SwarmNode *node;
    struct GossipPeer *next;
};

struct GossipNetwork {
    struct GossipPeer *peers;
    size_t peer_count;
    GossipTransportStats stats[GOSSIP_TRANSPORT_COUNT];
};

static int is_digit_string(const char *value, size_t len) {
    if (!value) {
        return 0;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!isdigit((unsigned char)value[i])) {
            return 0;
        }
    }
    return 1;
}

GossipNetwork *gossip_network_create(void) {
    GossipNetwork *network = calloc(1, sizeof(GossipNetwork));
    return network;
}

void gossip_network_destroy(GossipNetwork *network) {
    if (!network) {
        return;
    }
    struct GossipPeer *peer = network->peers;
    while (peer) {
        struct GossipPeer *next = peer->next;
        free(peer);
        peer = next;
    }
    free(network);
}

static struct GossipPeer *find_peer(GossipNetwork *network, const char *node_id) {
    for (struct GossipPeer *peer = network ? network->peers : NULL; peer; peer = peer->next) {
        if (strncmp(peer->node_id, node_id, SWARM_NODE_ID_DIGITS) == 0) {
            return peer;
        }
    }
    return NULL;
}

int gossip_network_add_peer(GossipNetwork *network, const char *node_id, SwarmNode *node) {
    if (!network || !node_id || !node) {
        return -1;
    }
    if (strlen(node_id) != SWARM_NODE_ID_DIGITS || !is_digit_string(node_id, SWARM_NODE_ID_DIGITS)) {
        return -1;
    }
    if (find_peer(network, node_id)) {
        return -1;
    }
    struct GossipPeer *peer = calloc(1, sizeof(struct GossipPeer));
    if (!peer) {
        return -1;
    }
    memcpy(peer->node_id, node_id, SWARM_NODE_ID_DIGITS);
    peer->node_id[SWARM_NODE_ID_DIGITS] = '\0';
    peer->node = node;
    peer->next = network->peers;
    network->peers = peer;
    network->peer_count++;
    return 0;
}

int gossip_network_remove_peer(GossipNetwork *network, const char *node_id) {
    if (!network || !node_id) {
        return -1;
    }
    struct GossipPeer *prev = NULL;
    struct GossipPeer *peer = network->peers;
    while (peer) {
        if (strncmp(peer->node_id, node_id, SWARM_NODE_ID_DIGITS) == 0) {
            if (prev) {
                prev->next = peer->next;
            } else {
                network->peers = peer->next;
            }
            free(peer);
            if (network->peer_count > 0) {
                network->peer_count--;
            }
            return 0;
        }
        prev = peer;
        peer = peer->next;
    }
    return -1;
}

int gossip_network_broadcast(GossipNetwork *network,
                              const char *source_id,
                              const SwarmFrame *frame,
                              GossipTransport transport) {
    if (!network || !source_id || !frame) {
        return -1;
    }
    if (strlen(source_id) != SWARM_NODE_ID_DIGITS || !is_digit_string(source_id, SWARM_NODE_ID_DIGITS)) {
        return -1;
    }
    size_t delivered = 0;
    for (struct GossipPeer *peer = network->peers; peer; peer = peer->next) {
        if (strncmp(peer->node_id, source_id, SWARM_NODE_ID_DIGITS) == 0) {
            continue;
        }
        SwarmAcceptDecision decision =
            swarm_node_submit_frame(peer->node, source_id, frame, 1 /* wait */);
        if (decision != SWARM_DECISION_ACCEPT) {
            log_warn("gossip: peer %s rejected frame type %d from %s",
                     peer->node_id,
                     (int)frame->type,
                     source_id);
            return -1;
        }
        delivered++;
    }
    if (transport < GOSSIP_TRANSPORT_COUNT) {
        network->stats[transport].datagrams += 1;
        network->stats[transport].frames_delivered += delivered;
    }
    return 0;
}

void gossip_network_get_stats(const GossipNetwork *network,
                              GossipTransportStats *out_stats,
                              size_t out_len) {
    if (!network || !out_stats || out_len == 0) {
        return;
    }
    size_t count = out_len < GOSSIP_TRANSPORT_COUNT ? out_len : GOSSIP_TRANSPORT_COUNT;
    for (size_t i = 0; i < count; ++i) {
        out_stats[i] = network->stats[i];
    }
}

int gossip_datagram_encode(GossipTransport transport,
                           const SwarmFrame *frame,
                           char *out,
                           size_t out_size,
                           size_t *written) {
    if (!frame || !out || out_size < 2) {
        return -1;
    }
    char prefix = transport == GOSSIP_TRANSPORT_QUIC ? 'Q' : 'U';
    out[0] = prefix;
    size_t payload = 0;
    if (swarm_frame_serialize(frame, out + 1, out_size - 1, &payload) != 0) {
        return -1;
    }
    if (written) {
        *written = payload + 1;
    }
    return 0;
}

int gossip_datagram_decode(const char *data,
                           size_t len,
                           GossipTransport *transport_out,
                           SwarmFrame *frame_out) {
    if (!data || !frame_out || len < 2) {
        return -1;
    }
    GossipTransport transport = GOSSIP_TRANSPORT_UDP;
    if (data[0] == 'Q') {
        transport = GOSSIP_TRANSPORT_QUIC;
    } else if (data[0] != 'U') {
        return -1;
    }
    if (transport_out) {
        *transport_out = transport;
    }
    if (swarm_frame_parse(data + 1, len - 1, frame_out) != 0) {
        return -1;
    }
    return 0;
}

int gossip_frame_from_fkv_delta(const fkv_delta_t *delta,
                                const char *prefix,
                                SwarmFrame *frame) {
    if (!delta || !prefix || !frame) {
        return -1;
    }
    if (strlen(prefix) != SWARM_PREFIX_DIGITS || !is_digit_string(prefix, SWARM_PREFIX_DIGITS)) {
        return -1;
    }
    if (delta->count > UINT16_MAX || delta->total_bytes > UINT32_MAX) {
        return -1;
    }
    frame->type = SWARM_FRAME_FKV_DELTA;
    memcpy(frame->payload.fkv_delta.prefix, prefix, SWARM_PREFIX_DIGITS);
    frame->payload.fkv_delta.prefix[SWARM_PREFIX_DIGITS] = '\0';
    frame->payload.fkv_delta.entry_count = (uint16_t)delta->count;
    frame->payload.fkv_delta.compressed_size = (uint32_t)delta->total_bytes;
    frame->payload.fkv_delta.checksum = delta->checksum;
    return 0;
}
