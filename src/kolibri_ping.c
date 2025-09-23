#include "kolibri_ping.h"
#include <string.h>

GlobalPingStats PING_STATS;

void init_ping_stats(void) {
    memset(&PING_STATS, 0, sizeof(PING_STATS));
}

void update_ping_stats(int peer_idx, uint64_t rtt) {
    if (peer_idx < 0 || peer_idx >= MAX_PEERS) return;
    
    PING_STATS.stats[peer_idx].active = true;
    PING_STATS.stats[peer_idx].last_seen = now_ms();
    PING_STATS.stats[peer_idx].rtt_sum += rtt;
    PING_STATS.stats[peer_idx].rtt_count++;
}

void check_peers_availability(void) {
    uint64_t now = now_ms();
    for (int i = 0; i < MAX_PEERS; i++) {
        if (PING_STATS.stats[i].active) {
            if (now - PING_STATS.stats[i].last_seen > 30000) { // 30 sec timeout
                PING_STATS.stats[i].active = false;
            }
        }
    }
}
