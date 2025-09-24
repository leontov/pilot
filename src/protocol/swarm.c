#include "protocol/swarm.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SWARM_PROTOCOL_VERSION_WIDTH 4
#define SWARM_FRAME_CODE_WIDTH 2

#define HELLO_VERSION_WIDTH 2
#define HELLO_SERVICES_WIDTH 4
#define HELLO_REPUTATION_WIDTH 3

#define PING_NONCE_WIDTH 10
#define PING_LATENCY_WIDTH 5

#define PROGRAM_POE_WIDTH 4
#define PROGRAM_MDL_WIDTH 5
#define PROGRAM_GAS_WIDTH 6

#define BLOCK_HEIGHT_WIDTH 8
#define BLOCK_POE_WIDTH 4
#define BLOCK_PROGRAM_COUNT_WIDTH 4

#define FKV_ENTRY_COUNT_WIDTH 3
#define FKV_SIZE_WIDTH 6
#define FKV_CHECKSUM_WIDTH 5

#define SWARM_REPUTATION_MAX 1000
#define SWARM_REPUTATION_MIN 0
#define SWARM_REPUTATION_START 600
#define SWARM_REPUTATION_BLOCK_THRESHOLD 200

typedef struct {
    double refill_per_sec;
    double burst;
} SwarmRateConfig;

static const SwarmRateConfig kRateConfig[SWARM_FRAME_TYPE_COUNT] = {
    [SWARM_FRAME_HELLO] = {0.1, 1.0},          // 1 HELLO every 10 seconds
    [SWARM_FRAME_PING] = {1.0, 3.0},           // burst of 3 pings, 1 ping per second refill
    [SWARM_FRAME_PROGRAM_OFFER] = {0.5, 5.0},  // up to 5 offers, new token every 2 seconds
    [SWARM_FRAME_BLOCK_OFFER] = {0.2, 2.0},    // 2 offers burst, 1 every 5 seconds
    [SWARM_FRAME_FKV_DELTA] = {0.3, 3.0},      // 3 deltas, ~1 every 3 seconds
};

static const uint8_t kFrameCode[SWARM_FRAME_TYPE_COUNT] = {10, 11, 12, 13, 14};

static uint64_t pow10u(unsigned width) {
    static const uint64_t pow10_table[] = {
        1ULL,
        10ULL,
        100ULL,
        1000ULL,
        10000ULL,
        100000ULL,
        1000000ULL,
        10000000ULL,
        100000000ULL,
        1000000000ULL,
        10000000000ULL,
        100000000000ULL,
        1000000000000ULL,
        10000000000000ULL,
        100000000000000ULL,
        1000000000000000ULL,
        10000000000000000ULL,
        100000000000000000ULL,
        1000000000000000000ULL
    };
    if (width < sizeof(pow10_table) / sizeof(pow10_table[0])) {
        return pow10_table[width];
    }
    return 0;
}

static int write_digits(char *dst, size_t remaining, unsigned width, uint64_t value) {
    uint64_t limit = pow10u(width);
    if (limit == 0 || value >= limit) {
        return -1;
    }
    if (remaining < width) {
        return -1;
    }
    for (unsigned i = 0; i < width; ++i) {
        unsigned idx = width - 1 - i;
        dst[idx] = (char)('0' + (value % 10));
        value /= 10;
    }
    return (int)width;
}

static bool digits_only(const char *src, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (!isdigit((unsigned char)src[i])) {
            return false;
        }
    }
    return true;
}

static int read_digits(const char *src, size_t len, uint64_t *value_out) {
    if (!digits_only(src, len)) {
        return -1;
    }
    uint64_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        value = value * 10ULL + (uint64_t)(src[i] - '0');
    }
    *value_out = value;
    return 0;
}

static int frame_type_from_code(uint64_t code, SwarmFrameType *type_out) {
    for (size_t i = 0; i < SWARM_FRAME_TYPE_COUNT; ++i) {
        if (kFrameCode[i] == code) {
            *type_out = (SwarmFrameType)i;
            return 0;
        }
    }
    return -1;
}

void swarm_rate_limiter_init(SwarmRateLimiter *limiter, uint64_t now_ms) {
    if (!limiter) {
        return;
    }
    for (size_t i = 0; i < SWARM_FRAME_TYPE_COUNT; ++i) {
        limiter->buckets[i].capacity = kRateConfig[i].burst;
        limiter->buckets[i].tokens = kRateConfig[i].burst;
        limiter->buckets[i].refill_rate = kRateConfig[i].refill_per_sec;
        limiter->buckets[i].last_refill_ms = now_ms;
    }
}

static void refill_bucket(SwarmTokenBucket *bucket, uint64_t now_ms) {
    if (!bucket) {
        return;
    }
    if (now_ms < bucket->last_refill_ms) {
        bucket->last_refill_ms = now_ms;
        return;
    }
    uint64_t delta_ms = now_ms - bucket->last_refill_ms;
    if (delta_ms == 0) {
        return;
    }
    double add_tokens = bucket->refill_rate * ((double)delta_ms / 1000.0);
    bucket->tokens += add_tokens;
    if (bucket->tokens > bucket->capacity) {
        bucket->tokens = bucket->capacity;
    }
    bucket->last_refill_ms = now_ms;
}

bool swarm_rate_limiter_allow(SwarmRateLimiter *limiter, SwarmFrameType type, uint64_t now_ms) {
    if (!limiter || type >= SWARM_FRAME_TYPE_COUNT) {
        return false;
    }
    SwarmTokenBucket *bucket = &limiter->buckets[type];
    refill_bucket(bucket, now_ms);
    if (bucket->tokens >= 1.0) {
        bucket->tokens -= 1.0;
        return true;
    }
    return false;
}

void swarm_reputation_init(SwarmReputation *rep) {
    if (!rep) {
        return;
    }
    rep->score = SWARM_REPUTATION_START;
    rep->infractions = 0;
    rep->successes = 0;
    rep->last_update_ms = 0;
}

static void clamp_reputation(SwarmReputation *rep) {
    if (!rep) {
        return;
    }
    if (rep->score > SWARM_REPUTATION_MAX) {
        rep->score = SWARM_REPUTATION_MAX;
    }
    if (rep->score < SWARM_REPUTATION_MIN) {
        rep->score = SWARM_REPUTATION_MIN;
    }
}

void swarm_reputation_reward(SwarmReputation *rep, uint16_t reward) {
    if (!rep) {
        return;
    }
    if (reward > 200) {
        reward = 200;
    }
    rep->score += (int32_t)reward;
    rep->successes += 1;
    clamp_reputation(rep);
}

void swarm_reputation_penalize(SwarmReputation *rep, uint16_t penalty) {
    if (!rep) {
        return;
    }
    if (penalty > 400) {
        penalty = 400;
    }
    if (rep->score > (int32_t)penalty) {
        rep->score -= (int32_t)penalty;
    } else {
        rep->score = SWARM_REPUTATION_MIN;
    }
    rep->infractions += 1;
    clamp_reputation(rep);
}

const char *swarm_reputation_class(const SwarmReputation *rep) {
    if (!rep) {
        return "unknown";
    }
    if (rep->score >= 850) {
        return "trusted";
    }
    if (rep->score >= 600) {
        return "stable";
    }
    if (rep->score >= 400) {
        return "neutral";
    }
    if (rep->score >= SWARM_REPUTATION_BLOCK_THRESHOLD) {
        return "suspect";
    }
    return "blocked";
}

void swarm_peer_state_init(SwarmPeerState *peer, uint64_t now_ms) {
    if (!peer) {
        return;
    }
    swarm_rate_limiter_init(&peer->limiter, now_ms);
    swarm_reputation_init(&peer->reputation);
}

static uint16_t frame_reward(SwarmFrameType type) {
    switch (type) {
        case SWARM_FRAME_HELLO:
            return 10;
        case SWARM_FRAME_PING:
            return 5;
        case SWARM_FRAME_PROGRAM_OFFER:
            return 25;
        case SWARM_FRAME_BLOCK_OFFER:
            return 40;
        case SWARM_FRAME_FKV_DELTA:
            return 15;
        default:
            return 5;
    }
}

SwarmAcceptDecision swarm_peer_should_accept(SwarmPeerState *peer, SwarmFrameType type, uint64_t now_ms) {
    if (!peer || type >= SWARM_FRAME_TYPE_COUNT) {
        return SWARM_DECISION_REPUTATION_BLOCKED;
    }
    peer->reputation.last_update_ms = now_ms;
    if (peer->reputation.score < SWARM_REPUTATION_BLOCK_THRESHOLD) {
        return SWARM_DECISION_REPUTATION_BLOCKED;
    }
    if (!swarm_rate_limiter_allow(&peer->limiter, type, now_ms)) {
        swarm_reputation_penalize(&peer->reputation, 20);
        return SWARM_DECISION_RATE_LIMITED;
    }
    return SWARM_DECISION_ACCEPT;
}

void swarm_peer_report_success(SwarmPeerState *peer, SwarmFrameType type) {
    if (!peer) {
        return;
    }
    swarm_reputation_reward(&peer->reputation, frame_reward(type));
}

void swarm_peer_report_violation(SwarmPeerState *peer, SwarmFrameType type) {
    (void)type;
    if (!peer) {
        return;
    }
    swarm_reputation_penalize(&peer->reputation, 80);
}

int swarm_frame_serialize(const SwarmFrame *frame, char *out, size_t out_size, size_t *written) {
    if (!frame || !out || out_size == 0) {
        return -1;
    }
    size_t offset = 0;
    int rc = write_digits(out + offset, out_size - offset, SWARM_PROTOCOL_VERSION_WIDTH, SWARM_PROTOCOL_VERSION);
    if (rc < 0) {
        return -1;
    }
    offset += (size_t)rc;

    uint8_t code = kFrameCode[frame->type];
    rc = write_digits(out + offset, out_size - offset, SWARM_FRAME_CODE_WIDTH, code);
    if (rc < 0) {
        return -1;
    }
    offset += (size_t)rc;

    switch (frame->type) {
        case SWARM_FRAME_HELLO: {
            rc = write_digits(out + offset, out_size - offset, HELLO_VERSION_WIDTH, frame->payload.hello.version);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            if (!digits_only(frame->payload.hello.node_id, SWARM_NODE_ID_DIGITS)) {
                return -1;
            }
            if (out_size - offset < SWARM_NODE_ID_DIGITS) return -1;
            memcpy(out + offset, frame->payload.hello.node_id, SWARM_NODE_ID_DIGITS);
            offset += SWARM_NODE_ID_DIGITS;
            rc = write_digits(out + offset, out_size - offset, HELLO_SERVICES_WIDTH, frame->payload.hello.services);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            rc = write_digits(out + offset, out_size - offset, HELLO_REPUTATION_WIDTH, frame->payload.hello.reputation);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            break;
        }
        case SWARM_FRAME_PING: {
            rc = write_digits(out + offset, out_size - offset, PING_NONCE_WIDTH, frame->payload.ping.nonce);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            rc = write_digits(out + offset, out_size - offset, PING_LATENCY_WIDTH, frame->payload.ping.latency_hint_ms);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            break;
        }
        case SWARM_FRAME_PROGRAM_OFFER: {
            if (!digits_only(frame->payload.program_offer.program_id, SWARM_PROGRAM_ID_DIGITS)) {
                return -1;
            }
            if (out_size - offset < SWARM_PROGRAM_ID_DIGITS) return -1;
            memcpy(out + offset, frame->payload.program_offer.program_id, SWARM_PROGRAM_ID_DIGITS);
            offset += SWARM_PROGRAM_ID_DIGITS;
            rc = write_digits(out + offset, out_size - offset, PROGRAM_POE_WIDTH, frame->payload.program_offer.poe_milli);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            rc = write_digits(out + offset, out_size - offset, PROGRAM_MDL_WIDTH, frame->payload.program_offer.mdl_score);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            rc = write_digits(out + offset, out_size - offset, PROGRAM_GAS_WIDTH, frame->payload.program_offer.gas_used);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            break;
        }
        case SWARM_FRAME_BLOCK_OFFER: {
            if (!digits_only(frame->payload.block_offer.block_id, SWARM_BLOCK_ID_DIGITS)) {
                return -1;
            }
            if (out_size - offset < SWARM_BLOCK_ID_DIGITS) return -1;
            memcpy(out + offset, frame->payload.block_offer.block_id, SWARM_BLOCK_ID_DIGITS);
            offset += SWARM_BLOCK_ID_DIGITS;
            rc = write_digits(out + offset, out_size - offset, BLOCK_HEIGHT_WIDTH, frame->payload.block_offer.height);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            rc = write_digits(out + offset, out_size - offset, BLOCK_POE_WIDTH, frame->payload.block_offer.poe_milli);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            rc = write_digits(out + offset, out_size - offset, BLOCK_PROGRAM_COUNT_WIDTH, frame->payload.block_offer.program_count);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            break;
        }
        case SWARM_FRAME_FKV_DELTA: {
            if (!digits_only(frame->payload.fkv_delta.prefix, SWARM_PREFIX_DIGITS)) {
                return -1;
            }
            if (out_size - offset < SWARM_PREFIX_DIGITS) return -1;
            memcpy(out + offset, frame->payload.fkv_delta.prefix, SWARM_PREFIX_DIGITS);
            offset += SWARM_PREFIX_DIGITS;
            rc = write_digits(out + offset, out_size - offset, FKV_ENTRY_COUNT_WIDTH, frame->payload.fkv_delta.entry_count);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            rc = write_digits(out + offset, out_size - offset, FKV_SIZE_WIDTH, frame->payload.fkv_delta.compressed_size);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            rc = write_digits(out + offset, out_size - offset, FKV_CHECKSUM_WIDTH, frame->payload.fkv_delta.checksum);
            if (rc < 0) return -1;
            offset += (size_t)rc;
            break;
        }
        default:
            return -1;
    }

    if (offset >= out_size) {
        return -1;
    }
    out[offset] = '\0';
    if (written) {
        *written = offset;
    }
    return 0;
}

int swarm_frame_parse(const char *data, size_t len, SwarmFrame *frame) {
    if (!data || !frame || len < (SWARM_PROTOCOL_VERSION_WIDTH + SWARM_FRAME_CODE_WIDTH)) {
        return -1;
    }
    if (!digits_only(data, len)) {
        return -1;
    }
    uint64_t proto = 0;
    if (read_digits(data, SWARM_PROTOCOL_VERSION_WIDTH, &proto) != 0) {
        return -1;
    }
    if (proto != SWARM_PROTOCOL_VERSION) {
        return -1;
    }
    uint64_t code = 0;
    if (read_digits(data + SWARM_PROTOCOL_VERSION_WIDTH, SWARM_FRAME_CODE_WIDTH, &code) != 0) {
        return -1;
    }
    SwarmFrameType type;
    if (frame_type_from_code(code, &type) != 0) {
        return -1;
    }
    frame->type = type;
    size_t offset = SWARM_PROTOCOL_VERSION_WIDTH + SWARM_FRAME_CODE_WIDTH;

    switch (type) {
        case SWARM_FRAME_HELLO: {
            uint64_t value = 0;
            if (offset + HELLO_VERSION_WIDTH + SWARM_NODE_ID_DIGITS + HELLO_SERVICES_WIDTH + HELLO_REPUTATION_WIDTH > len) {
                return -1;
            }
            if (read_digits(data + offset, HELLO_VERSION_WIDTH, &value) != 0) return -1;
            frame->payload.hello.version = (uint16_t)value;
            offset += HELLO_VERSION_WIDTH;
            memcpy(frame->payload.hello.node_id, data + offset, SWARM_NODE_ID_DIGITS);
            frame->payload.hello.node_id[SWARM_NODE_ID_DIGITS] = '\0';
            offset += SWARM_NODE_ID_DIGITS;
            if (read_digits(data + offset, HELLO_SERVICES_WIDTH, &value) != 0) return -1;
            frame->payload.hello.services = (uint16_t)value;
            offset += HELLO_SERVICES_WIDTH;
            if (read_digits(data + offset, HELLO_REPUTATION_WIDTH, &value) != 0) return -1;
            frame->payload.hello.reputation = (uint16_t)value;
            offset += HELLO_REPUTATION_WIDTH;
            break;
        }
        case SWARM_FRAME_PING: {
            uint64_t value = 0;
            if (offset + PING_NONCE_WIDTH + PING_LATENCY_WIDTH > len) return -1;
            if (read_digits(data + offset, PING_NONCE_WIDTH, &value) != 0) return -1;
            frame->payload.ping.nonce = (uint32_t)value;
            offset += PING_NONCE_WIDTH;
            if (read_digits(data + offset, PING_LATENCY_WIDTH, &value) != 0) return -1;
            frame->payload.ping.latency_hint_ms = (uint32_t)value;
            offset += PING_LATENCY_WIDTH;
            break;
        }
        case SWARM_FRAME_PROGRAM_OFFER: {
            uint64_t value = 0;
            if (offset + SWARM_PROGRAM_ID_DIGITS + PROGRAM_POE_WIDTH + PROGRAM_MDL_WIDTH + PROGRAM_GAS_WIDTH > len) return -1;
            memcpy(frame->payload.program_offer.program_id, data + offset, SWARM_PROGRAM_ID_DIGITS);
            frame->payload.program_offer.program_id[SWARM_PROGRAM_ID_DIGITS] = '\0';
            offset += SWARM_PROGRAM_ID_DIGITS;
            if (read_digits(data + offset, PROGRAM_POE_WIDTH, &value) != 0) return -1;
            frame->payload.program_offer.poe_milli = (uint16_t)value;
            offset += PROGRAM_POE_WIDTH;
            if (read_digits(data + offset, PROGRAM_MDL_WIDTH, &value) != 0) return -1;
            frame->payload.program_offer.mdl_score = (uint16_t)value;
            offset += PROGRAM_MDL_WIDTH;
            if (read_digits(data + offset, PROGRAM_GAS_WIDTH, &value) != 0) return -1;
            frame->payload.program_offer.gas_used = (uint32_t)value;
            offset += PROGRAM_GAS_WIDTH;
            break;
        }
        case SWARM_FRAME_BLOCK_OFFER: {
            uint64_t value = 0;
            if (offset + SWARM_BLOCK_ID_DIGITS + BLOCK_HEIGHT_WIDTH + BLOCK_POE_WIDTH + BLOCK_PROGRAM_COUNT_WIDTH > len) return -1;
            memcpy(frame->payload.block_offer.block_id, data + offset, SWARM_BLOCK_ID_DIGITS);
            frame->payload.block_offer.block_id[SWARM_BLOCK_ID_DIGITS] = '\0';
            offset += SWARM_BLOCK_ID_DIGITS;
            if (read_digits(data + offset, BLOCK_HEIGHT_WIDTH, &value) != 0) return -1;
            frame->payload.block_offer.height = (uint32_t)value;
            offset += BLOCK_HEIGHT_WIDTH;
            if (read_digits(data + offset, BLOCK_POE_WIDTH, &value) != 0) return -1;
            frame->payload.block_offer.poe_milli = (uint16_t)value;
            offset += BLOCK_POE_WIDTH;
            if (read_digits(data + offset, BLOCK_PROGRAM_COUNT_WIDTH, &value) != 0) return -1;
            frame->payload.block_offer.program_count = (uint16_t)value;
            offset += BLOCK_PROGRAM_COUNT_WIDTH;
            break;
        }
        case SWARM_FRAME_FKV_DELTA: {
            uint64_t value = 0;
            if (offset + SWARM_PREFIX_DIGITS + FKV_ENTRY_COUNT_WIDTH + FKV_SIZE_WIDTH + FKV_CHECKSUM_WIDTH > len) return -1;
            memcpy(frame->payload.fkv_delta.prefix, data + offset, SWARM_PREFIX_DIGITS);
            frame->payload.fkv_delta.prefix[SWARM_PREFIX_DIGITS] = '\0';
            offset += SWARM_PREFIX_DIGITS;
            if (read_digits(data + offset, FKV_ENTRY_COUNT_WIDTH, &value) != 0) return -1;
            frame->payload.fkv_delta.entry_count = (uint16_t)value;
            offset += FKV_ENTRY_COUNT_WIDTH;
            if (read_digits(data + offset, FKV_SIZE_WIDTH, &value) != 0) return -1;
            frame->payload.fkv_delta.compressed_size = (uint32_t)value;
            offset += FKV_SIZE_WIDTH;
            if (read_digits(data + offset, FKV_CHECKSUM_WIDTH, &value) != 0) return -1;
            frame->payload.fkv_delta.checksum = (uint16_t)value;
            offset += FKV_CHECKSUM_WIDTH;
            break;
        }
        default:
            return -1;
    }

    if (offset != len) {
        return -1;
    }
    return 0;
}
