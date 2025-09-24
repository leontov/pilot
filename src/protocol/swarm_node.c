#define _POSIX_C_SOURCE 200809L

#include "protocol/swarm_node.h"

#include "protocol/swarm.h"
#include "util/log.h"

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SWARM_MAX_PEERS 32
#define SWARM_OUTBOUND_CAPACITY 64

typedef struct SwarmFrameEvent {
    SwarmFrame frame;
    char peer_id[SWARM_NODE_ID_DIGITS + 1];
    int wait;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int completed;
    SwarmAcceptDecision decision;
    struct SwarmFrameEvent *next;
} SwarmFrameEvent;

typedef struct {
    SwarmFrameEvent *head;
    SwarmFrameEvent *tail;
    int shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} SwarmEventQueue;

typedef struct {
    SwarmOutboundFrame buffer[SWARM_OUTBOUND_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} SwarmOutbox;

typedef struct {
    int in_use;
    char peer_id[SWARM_NODE_ID_DIGITS + 1];
    SwarmPeerState state;
    uint32_t frames[SWARM_FRAME_TYPE_COUNT];
    uint64_t last_seen_ms;
    SwarmHelloPayload hello;
    SwarmPingPayload ping;
    SwarmProgramOfferPayload program_offer;
    SwarmBlockOfferPayload block_offer;
    SwarmFkvDeltaPayload fkv_delta;
} SwarmPeerContext;

struct SwarmNode {
    SwarmNodeOptions options;
    SwarmEventQueue queue;
    SwarmOutbox outbox;
    SwarmPeerContext peers[SWARM_MAX_PEERS];
    pthread_mutex_t peers_mutex;
    pthread_t thread;
    int thread_running;
    int initialized;
};

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}

static void event_queue_init(SwarmEventQueue *queue) {
    memset(queue, 0, sizeof(*queue));
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

static void event_queue_destroy(SwarmEventQueue *queue) {
    if (!queue) {
        return;
    }
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

static int event_queue_push(SwarmEventQueue *queue, SwarmFrameEvent *event) {
    pthread_mutex_lock(&queue->mutex);
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    event->next = NULL;
    if (!queue->head) {
        queue->head = queue->tail = event;
    } else {
        queue->tail->next = event;
        queue->tail = event;
    }
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return 0;
}

static SwarmFrameEvent *event_queue_pop(SwarmEventQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    while (!queue->head && !queue->shutdown) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    if (!queue->head) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }
    SwarmFrameEvent *event = queue->head;
    queue->head = event->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    pthread_mutex_unlock(&queue->mutex);
    return event;
}

static void event_queue_shutdown(SwarmEventQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = 1;
    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
}

static void outbox_init(SwarmOutbox *outbox) {
    memset(outbox, 0, sizeof(*outbox));
    pthread_mutex_init(&outbox->mutex, NULL);
    pthread_cond_init(&outbox->cond, NULL);
}

static void outbox_destroy(SwarmOutbox *outbox) {
    pthread_mutex_destroy(&outbox->mutex);
    pthread_cond_destroy(&outbox->cond);
}

static void outbox_push(SwarmOutbox *outbox, const char *peer_id, const SwarmFrame *frame) {
    pthread_mutex_lock(&outbox->mutex);
    if (outbox->count == SWARM_OUTBOUND_CAPACITY) {
        outbox->head = (outbox->head + 1) % SWARM_OUTBOUND_CAPACITY;
        outbox->count--;
    }
    size_t idx = outbox->tail;
    memcpy(&outbox->buffer[idx].frame, frame, sizeof(SwarmFrame));
    if (peer_id) {
        strncpy(outbox->buffer[idx].peer_id, peer_id, SWARM_NODE_ID_DIGITS);
        outbox->buffer[idx].peer_id[SWARM_NODE_ID_DIGITS] = '\0';
    } else {
        outbox->buffer[idx].peer_id[0] = '\0';
    }
    outbox->tail = (outbox->tail + 1) % SWARM_OUTBOUND_CAPACITY;
    outbox->count++;
    pthread_cond_signal(&outbox->cond);
    pthread_mutex_unlock(&outbox->mutex);
}

static int outbox_pop(SwarmOutbox *outbox, SwarmOutboundFrame *out, uint32_t timeout_ms) {
    int rc = 0;
    struct timespec ts;
    pthread_mutex_lock(&outbox->mutex);
    while (outbox->count == 0) {
        if (timeout_ms == 0) {
            pthread_mutex_unlock(&outbox->mutex);
            return -1;
        }
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000u;
        ts.tv_nsec += (timeout_ms % 1000u) * 1000000ull;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        rc = pthread_cond_timedwait(&outbox->cond, &outbox->mutex, &ts);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&outbox->mutex);
            return -1;
        }
    }
    size_t idx = outbox->head;
    if (out) {
        memcpy(out, &outbox->buffer[idx], sizeof(SwarmOutboundFrame));
    }
    outbox->head = (outbox->head + 1) % SWARM_OUTBOUND_CAPACITY;
    outbox->count--;
    pthread_mutex_unlock(&outbox->mutex);
    return 0;
}

static SwarmPeerContext *find_peer(SwarmNode *node, const char *peer_id) {
    for (size_t i = 0; i < SWARM_MAX_PEERS; ++i) {
        if (node->peers[i].in_use && strncmp(node->peers[i].peer_id, peer_id, SWARM_NODE_ID_DIGITS) == 0) {
            return &node->peers[i];
        }
    }
    return NULL;
}

static SwarmPeerContext *get_or_create_peer(SwarmNode *node, const char *peer_id, uint64_t now_ms) {
    SwarmPeerContext *ctx = find_peer(node, peer_id);
    if (ctx) {
        return ctx;
    }
    for (size_t i = 0; i < SWARM_MAX_PEERS; ++i) {
        if (!node->peers[i].in_use) {
            ctx = &node->peers[i];
            memset(ctx, 0, sizeof(*ctx));
            ctx->in_use = 1;
            strncpy(ctx->peer_id, peer_id, SWARM_NODE_ID_DIGITS);
            ctx->peer_id[SWARM_NODE_ID_DIGITS] = '\0';
            swarm_peer_state_init(&ctx->state, now_ms);
            ctx->last_seen_ms = now_ms;
            return ctx;
        }
    }
    return NULL;
}

static void handle_hello(SwarmNode *node, SwarmPeerContext *peer, const SwarmFrame *frame) {
    peer->hello = frame->payload.hello;
    peer->frames[SWARM_FRAME_HELLO] += 1;
    SwarmFrame reply = {.type = SWARM_FRAME_HELLO};
    reply.payload.hello.version = node->options.version;
    strncpy(reply.payload.hello.node_id, node->options.node_id, SWARM_NODE_ID_DIGITS);
    reply.payload.hello.node_id[SWARM_NODE_ID_DIGITS] = '\0';
    reply.payload.hello.services = node->options.services;
    reply.payload.hello.reputation = (uint16_t)peer->state.reputation.score;
    outbox_push(&node->outbox, peer->peer_id, &reply);
}

static void handle_ping(SwarmNode *node, SwarmPeerContext *peer, const SwarmFrame *frame, uint64_t now_ms) {
    (void)now_ms;
    peer->ping = frame->payload.ping;
    peer->frames[SWARM_FRAME_PING] += 1;
    SwarmFrame reply = {.type = SWARM_FRAME_PING};
    reply.payload.ping.nonce = frame->payload.ping.nonce;
    uint32_t latency = frame->payload.ping.latency_hint_ms;
    if (latency == 0) {
        latency = 1;
    }
    reply.payload.ping.latency_hint_ms = latency;
    outbox_push(&node->outbox, peer->peer_id, &reply);
}

static void handle_program_offer(SwarmNode *node, SwarmPeerContext *peer, const SwarmFrame *frame) {
    (void)node;
    peer->program_offer = frame->payload.program_offer;
    peer->frames[SWARM_FRAME_PROGRAM_OFFER] += 1;
}

static void handle_block_offer(SwarmNode *node, SwarmPeerContext *peer, const SwarmFrame *frame) {
    (void)node;
    peer->block_offer = frame->payload.block_offer;
    peer->frames[SWARM_FRAME_BLOCK_OFFER] += 1;
}

static void handle_fkv_delta(SwarmNode *node, SwarmPeerContext *peer, const SwarmFrame *frame) {
    (void)node;
    peer->fkv_delta = frame->payload.fkv_delta;
    peer->frames[SWARM_FRAME_FKV_DELTA] += 1;
}

static SwarmAcceptDecision process_event(SwarmNode *node, SwarmFrameEvent *event) {
    uint64_t now_ms = monotonic_ms();
    pthread_mutex_lock(&node->peers_mutex);
    SwarmPeerContext *peer = get_or_create_peer(node, event->peer_id, now_ms);
    if (!peer) {
        pthread_mutex_unlock(&node->peers_mutex);
        log_warn("swarm: dropping frame from %s (too many peers)", event->peer_id);
        return SWARM_DECISION_REPUTATION_BLOCKED;
    }
    peer->last_seen_ms = now_ms;
    SwarmAcceptDecision decision = swarm_peer_should_accept(&peer->state, event->frame.type, now_ms);
    if (decision != SWARM_DECISION_ACCEPT) {
        pthread_mutex_unlock(&node->peers_mutex);
        return decision;
    }
    switch (event->frame.type) {
        case SWARM_FRAME_HELLO:
            handle_hello(node, peer, &event->frame);
            break;
        case SWARM_FRAME_PING:
            handle_ping(node, peer, &event->frame, now_ms);
            break;
        case SWARM_FRAME_PROGRAM_OFFER:
            handle_program_offer(node, peer, &event->frame);
            break;
        case SWARM_FRAME_BLOCK_OFFER:
            handle_block_offer(node, peer, &event->frame);
            break;
        case SWARM_FRAME_FKV_DELTA:
            handle_fkv_delta(node, peer, &event->frame);
            break;
        default:
            swarm_peer_report_violation(&peer->state, event->frame.type);
            pthread_mutex_unlock(&node->peers_mutex);
            return SWARM_DECISION_REPUTATION_BLOCKED;
    }
    swarm_peer_report_success(&peer->state, event->frame.type);
    pthread_mutex_unlock(&node->peers_mutex);
    return SWARM_DECISION_ACCEPT;
}

static void complete_event(SwarmFrameEvent *event, SwarmAcceptDecision decision) {
    if (event->wait) {
        pthread_mutex_lock(&event->mutex);
        event->decision = decision;
        event->completed = 1;
        pthread_cond_signal(&event->cond);
        pthread_mutex_unlock(&event->mutex);
    } else {
        free(event);
    }
}

static void *swarm_thread_main(void *arg) {
    SwarmNode *node = (SwarmNode *)arg;
    while (1) {
        SwarmFrameEvent *event = event_queue_pop(&node->queue);
        if (!event) {
            break;
        }
        SwarmAcceptDecision decision = process_event(node, event);
        complete_event(event, decision);
    }
    return NULL;
}

SwarmNode *swarm_node_create(const SwarmNodeOptions *opts) {
    if (!opts) {
        return NULL;
    }
    SwarmNode *node = calloc(1, sizeof(SwarmNode));
    if (!node) {
        return NULL;
    }
    memcpy(&node->options, opts, sizeof(SwarmNodeOptions));
    node->options.node_id[SWARM_NODE_ID_DIGITS] = '\0';
    event_queue_init(&node->queue);
    outbox_init(&node->outbox);
    pthread_mutex_init(&node->peers_mutex, NULL);
    node->initialized = 1;
    return node;
}

int swarm_node_start(SwarmNode *node) {
    if (!node || !node->initialized) {
        return -1;
    }
    if (node->thread_running) {
        return 0;
    }
    node->thread_running = 1;
    if (pthread_create(&node->thread, NULL, swarm_thread_main, node) != 0) {
        node->thread_running = 0;
        return -1;
    }
    return 0;
}

void swarm_node_stop(SwarmNode *node) {
    if (!node || !node->thread_running) {
        return;
    }
    event_queue_shutdown(&node->queue);
    pthread_join(node->thread, NULL);
    node->thread_running = 0;
}

void swarm_node_destroy(SwarmNode *node) {
    if (!node) {
        return;
    }
    if (node->thread_running) {
        swarm_node_stop(node);
    }
    if (node->initialized) {
        event_queue_destroy(&node->queue);
        outbox_destroy(&node->outbox);
        pthread_mutex_destroy(&node->peers_mutex);
        node->initialized = 0;
    }
    free(node);
}

SwarmAcceptDecision swarm_node_submit_frame(SwarmNode *node,
                                            const char *peer_id,
                                            const SwarmFrame *frame,
                                            int wait_for_completion) {
    if (!node || !peer_id || !frame || !node->thread_running) {
        return SWARM_DECISION_REPUTATION_BLOCKED;
    }
    SwarmFrameEvent *event = calloc(1, sizeof(SwarmFrameEvent));
    if (!event) {
        return SWARM_DECISION_REPUTATION_BLOCKED;
    }
    memcpy(&event->frame, frame, sizeof(SwarmFrame));
    strncpy(event->peer_id, peer_id, SWARM_NODE_ID_DIGITS);
    event->peer_id[SWARM_NODE_ID_DIGITS] = '\0';
    event->wait = wait_for_completion ? 1 : 0;
    if (event->wait) {
        pthread_mutex_init(&event->mutex, NULL);
        pthread_cond_init(&event->cond, NULL);
    }
    if (event_queue_push(&node->queue, event) != 0) {
        if (event->wait) {
            pthread_cond_destroy(&event->cond);
            pthread_mutex_destroy(&event->mutex);
        }
        free(event);
        return SWARM_DECISION_REPUTATION_BLOCKED;
    }
    if (!event->wait) {
        return SWARM_DECISION_ACCEPT;
    }
    pthread_mutex_lock(&event->mutex);
    while (!event->completed) {
        pthread_cond_wait(&event->cond, &event->mutex);
    }
    SwarmAcceptDecision decision = event->decision;
    pthread_mutex_unlock(&event->mutex);
    pthread_cond_destroy(&event->cond);
    pthread_mutex_destroy(&event->mutex);
    free(event);
    return decision;
}

int swarm_node_poll_outbound(SwarmNode *node,
                             SwarmOutboundFrame *out,
                             uint32_t timeout_ms) {
    if (!node) {
        return -1;
    }
    return outbox_pop(&node->outbox, out, timeout_ms);
}

int swarm_node_get_peer_snapshot(SwarmNode *node,
                                 const char *peer_id,
                                 SwarmPeerSnapshot *out) {
    if (!node || !peer_id || !out) {
        return -1;
    }
    pthread_mutex_lock(&node->peers_mutex);
    SwarmPeerContext *peer = find_peer(node, peer_id);
    if (!peer) {
        pthread_mutex_unlock(&node->peers_mutex);
        return -1;
    }
    memset(out, 0, sizeof(*out));
    strncpy(out->peer_id, peer->peer_id, SWARM_NODE_ID_DIGITS);
    out->peer_id[SWARM_NODE_ID_DIGITS] = '\0';
    memcpy(out->frames, peer->frames, sizeof(peer->frames));
    out->reputation_score = peer->state.reputation.score;
    out->infractions = peer->state.reputation.infractions;
    out->successes = peer->state.reputation.successes;
    out->last_seen_ms = peer->last_seen_ms;
    out->hello = peer->hello;
    out->ping = peer->ping;
    out->program_offer = peer->program_offer;
    out->block_offer = peer->block_offer;
    out->fkv_delta = peer->fkv_delta;
    pthread_mutex_unlock(&node->peers_mutex);
    return 0;
}
