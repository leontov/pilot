#ifndef KOLIBRI_PING_H
#define KOLIBRI_PING_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PEERS 256

typedef struct {
    bool active;
    uint64_t last_seen;
    uint64_t rtt_sum;
    uint32_t rtt_count;
} PingStats;

typedef struct {
    PingStats stats[MAX_PEERS];
    int count;
} GlobalPingStats;

extern GlobalPingStats PING_STATS;
extern uint64_t now_ms(void);

void init_ping_stats(void);
void update_ping_stats(int peer_idx, uint64_t rtt);
void check_peers_availability(void);

#endif
