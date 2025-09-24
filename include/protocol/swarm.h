#ifndef KOLIBRI_PROTOCOL_SWARM_H
#define KOLIBRI_PROTOCOL_SWARM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SWARM_FRAME_HELLO = 0,
    SWARM_FRAME_PING = 1,
    SWARM_FRAME_PROGRAM_OFFER = 2,
    SWARM_FRAME_BLOCK_OFFER = 3,
    SWARM_FRAME_FKV_DELTA = 4,
    SWARM_FRAME_TYPE_COUNT
} SwarmFrameType;

#define SWARM_PROTOCOL_VERSION 1
#define SWARM_NODE_ID_DIGITS 16
#define SWARM_PROGRAM_ID_DIGITS 16
#define SWARM_BLOCK_ID_DIGITS 16
#define SWARM_PREFIX_DIGITS 12

#define SWARM_MAX_FRAME_SIZE 128

typedef struct {
    uint16_t version;
    char node_id[SWARM_NODE_ID_DIGITS + 1];
    uint16_t services;
    uint16_t reputation;
} SwarmHelloPayload;

typedef struct {
    uint32_t nonce;
    uint32_t latency_hint_ms;
} SwarmPingPayload;

typedef struct {
    char program_id[SWARM_PROGRAM_ID_DIGITS + 1];
    uint16_t poe_milli;
    uint16_t mdl_score;
    uint32_t gas_used;
} SwarmProgramOfferPayload;

typedef struct {
    char block_id[SWARM_BLOCK_ID_DIGITS + 1];
    uint32_t height;
    uint16_t poe_milli;
    uint16_t program_count;
} SwarmBlockOfferPayload;

typedef struct {
    char prefix[SWARM_PREFIX_DIGITS + 1];
    uint16_t entry_count;
    uint32_t compressed_size;
    uint16_t checksum;
} SwarmFkvDeltaPayload;

typedef struct {
    SwarmFrameType type;
    union {
        SwarmHelloPayload hello;
        SwarmPingPayload ping;
        SwarmProgramOfferPayload program_offer;
        SwarmBlockOfferPayload block_offer;
        SwarmFkvDeltaPayload fkv_delta;
    } payload;
} SwarmFrame;

typedef struct {
    double tokens;
    double capacity;
    double refill_rate;
    uint64_t last_refill_ms;
} SwarmTokenBucket;

typedef struct {
    SwarmTokenBucket buckets[SWARM_FRAME_TYPE_COUNT];
} SwarmRateLimiter;

typedef struct {
    int32_t score;         // 0..1000
    uint32_t infractions;
    uint32_t successes;
    uint64_t last_update_ms;
} SwarmReputation;

typedef struct {
    SwarmRateLimiter limiter;
    SwarmReputation reputation;
} SwarmPeerState;

typedef enum {
    SWARM_DECISION_ACCEPT = 0,
    SWARM_DECISION_RATE_LIMITED = 1,
    SWARM_DECISION_REPUTATION_BLOCKED = 2
} SwarmAcceptDecision;

void swarm_rate_limiter_init(SwarmRateLimiter *limiter, uint64_t now_ms);
bool swarm_rate_limiter_allow(SwarmRateLimiter *limiter, SwarmFrameType type, uint64_t now_ms);

void swarm_reputation_init(SwarmReputation *rep);
void swarm_reputation_reward(SwarmReputation *rep, uint16_t reward);
void swarm_reputation_penalize(SwarmReputation *rep, uint16_t penalty);
const char *swarm_reputation_class(const SwarmReputation *rep);

void swarm_peer_state_init(SwarmPeerState *peer, uint64_t now_ms);
SwarmAcceptDecision swarm_peer_should_accept(SwarmPeerState *peer, SwarmFrameType type, uint64_t now_ms);
void swarm_peer_report_success(SwarmPeerState *peer, SwarmFrameType type);
void swarm_peer_report_violation(SwarmPeerState *peer, SwarmFrameType type);

int swarm_frame_serialize(const SwarmFrame *frame, char *out, size_t out_size, size_t *written);
int swarm_frame_parse(const char *data, size_t len, SwarmFrame *frame);

#endif // KOLIBRI_PROTOCOL_SWARM_H
