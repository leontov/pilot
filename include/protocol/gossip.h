#ifndef KOLIBRI_PROTOCOL_GOSSIP_H
#define KOLIBRI_PROTOCOL_GOSSIP_H

#include "fkv/fkv.h"
#include "protocol/swarm.h"
#include "protocol/swarm_node.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GOSSIP_TRANSPORT_UDP = 0,
    GOSSIP_TRANSPORT_QUIC = 1,
    GOSSIP_TRANSPORT_COUNT
} GossipTransport;

typedef struct {
    size_t datagrams;
    size_t frames_delivered;
} GossipTransportStats;

typedef struct GossipNetwork GossipNetwork;

GossipNetwork *gossip_network_create(void);
void gossip_network_destroy(GossipNetwork *network);
int gossip_network_add_peer(GossipNetwork *network, const char *node_id, SwarmNode *node);
int gossip_network_remove_peer(GossipNetwork *network, const char *node_id);
int gossip_network_broadcast(GossipNetwork *network,
                              const char *source_id,
                              const SwarmFrame *frame,
                              GossipTransport transport);
void gossip_network_get_stats(const GossipNetwork *network,
                              GossipTransportStats *out_stats,
                              size_t out_len);

int gossip_datagram_encode(GossipTransport transport,
                           const SwarmFrame *frame,
                           char *out,
                           size_t out_size,
                           size_t *written);
int gossip_datagram_decode(const char *data,
                           size_t len,
                           GossipTransport *transport_out,
                           SwarmFrame *frame_out);

int gossip_frame_from_fkv_delta(const fkv_delta_t *delta,
                                const char *prefix,
                                SwarmFrame *frame);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_PROTOCOL_GOSSIP_H */
