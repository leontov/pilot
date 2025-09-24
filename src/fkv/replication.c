#include "fkv/replication.h"

#include "fkv/fkv.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

int fkv_replication_build_delta(const uint8_t *prefix_digits,
                                size_t prefix_len,
                                SwarmFrame *frame) {
    if (!frame) {
        errno = EINVAL;
        return -1;
    }
    if (prefix_len > (SWARM_PREFIX_DIGITS - 2)) {
        errno = EINVAL;
        return -1;
    }
    memset(frame, 0, sizeof(*frame));
    frame->type = SWARM_FRAME_FKV_DELTA;
    if (swarm_fkv_prefix_encode(prefix_digits, prefix_len, frame->payload.fkv_delta.prefix) != 0) {
        errno = EINVAL;
        return -1;
    }
    uint8_t prefix_buf[SWARM_PREFIX_DIGITS];
    if (prefix_len > 0 && prefix_digits) {
        memcpy(prefix_buf, prefix_digits, prefix_len);
    }
    fkv_iter_t it = {0};
    if (fkv_get_prefix(prefix_len > 0 ? prefix_buf : NULL, prefix_len, &it, 0) != 0) {
        return -1;
    }
    size_t raw_size = 0;
    for (size_t i = 0; i < it.count; ++i) {
        raw_size += 1 + 1 + sizeof(uint64_t) + sizeof(uint64_t);
        raw_size += it.entries[i].key_len;
        raw_size += it.entries[i].value_len;
    }
    uint8_t *raw = NULL;
    if (raw_size > 0) {
        raw = malloc(raw_size);
        if (!raw) {
            fkv_iter_free(&it);
            return -1;
        }
        size_t offset = 0;
        for (size_t i = 0; i < it.count; ++i) {
            raw[offset++] = 1; /* WAL_OP_PUT */
            raw[offset++] = (uint8_t)it.entries[i].type;
            uint64_t key_len = (uint64_t)it.entries[i].key_len;
            memcpy(raw + offset, &key_len, sizeof(uint64_t));
            offset += sizeof(uint64_t);
            if (key_len > 0) {
                memcpy(raw + offset, it.entries[i].key, it.entries[i].key_len);
                offset += it.entries[i].key_len;
            }
            uint64_t value_len = (uint64_t)it.entries[i].value_len;
            memcpy(raw + offset, &value_len, sizeof(uint64_t));
            offset += sizeof(uint64_t);
            if (value_len > 0) {
                memcpy(raw + offset, it.entries[i].value, it.entries[i].value_len);
                offset += it.entries[i].value_len;
            }
        }
    }
    if (raw_size > UINT32_MAX) {
        free(raw);
        fkv_iter_free(&it);
        errno = EOVERFLOW;
        return -1;
    }
    frame->payload.fkv_delta.entry_count = (uint16_t)it.count;
    frame->payload.fkv_delta.raw_size = (uint32_t)raw_size;
    uint16_t checksum = swarm_crc16(raw, raw_size);
    frame->payload.fkv_delta.checksum = checksum;
    fkv_iter_free(&it);
    if (raw_size == 0) {
        frame->payload.fkv_delta.compressed_size = 0;
        frame->payload.fkv_delta.data = NULL;
        frame->payload.fkv_delta.data_len = 0;
        return 0;
    }
    uLongf dest_capacity = compressBound(raw_size);
    uint8_t *compressed = malloc(dest_capacity);
    if (!compressed) {
        free(raw);
        return -1;
    }
    uLongf compressed_len = dest_capacity;
    if (compress2(compressed, &compressed_len, raw, raw_size, Z_BEST_SPEED) != Z_OK) {
        free(raw);
        free(compressed);
        return -1;
    }
    free(raw);
    if (compressed_len > UINT32_MAX) {
        free(compressed);
        errno = EOVERFLOW;
        return -1;
    }
    frame->payload.fkv_delta.compressed_size = (uint32_t)compressed_len;
    frame->payload.fkv_delta.data = compressed;
    frame->payload.fkv_delta.data_len = (size_t)compressed_len;
    return 0;
}

int fkv_replication_apply_delta(const SwarmFrame *frame) {
    if (!frame || frame->type != SWARM_FRAME_FKV_DELTA) {
        errno = EINVAL;
        return -1;
    }
    const SwarmFkvDeltaPayload *delta = &frame->payload.fkv_delta;
    if (delta->compressed_size != (uint32_t)delta->data_len) {
        errno = EINVAL;
        return -1;
    }
    if (delta->raw_size == 0) {
        return 0;
    }
    if (!delta->data) {
        errno = EINVAL;
        return -1;
    }
    uint8_t *raw = malloc(delta->raw_size);
    if (!raw) {
        return -1;
    }
    uLongf raw_len = delta->raw_size;
    if (uncompress(raw, &raw_len, delta->data, delta->data_len) != Z_OK || raw_len != delta->raw_size) {
        free(raw);
        errno = EINVAL;
        return -1;
    }
    if (swarm_crc16(raw, raw_len) != delta->checksum) {
        free(raw);
        errno = EINVAL;
        return -1;
    }
    size_t offset = 0;
    while (offset < raw_len) {
        if (raw_len - offset < 1 + 1 + sizeof(uint64_t) + sizeof(uint64_t)) {
            free(raw);
            errno = EINVAL;
            return -1;
        }
        uint8_t opcode = raw[offset++];
        if (opcode != 1) {
            free(raw);
            errno = EINVAL;
            return -1;
        }
        uint8_t type = raw[offset++];
        uint64_t key_len = 0;
        memcpy(&key_len, raw + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        if (key_len > raw_len - offset) {
            free(raw);
            errno = EINVAL;
            return -1;
        }
        const uint8_t *key = key_len ? raw + offset : NULL;
        offset += (size_t)key_len;
        uint64_t value_len = 0;
        memcpy(&value_len, raw + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        if (value_len > raw_len - offset) {
            free(raw);
            errno = EINVAL;
            return -1;
        }
        const uint8_t *value = value_len ? raw + offset : NULL;
        offset += (size_t)value_len;
        if (fkv_put(key, (size_t)key_len, value, (size_t)value_len, (fkv_entry_type_t)type) != 0) {
            free(raw);
            return -1;
        }
    }
    free(raw);
    return 0;
}

void fkv_replication_free_delta(SwarmFrame *frame) {
    if (!frame || frame->type != SWARM_FRAME_FKV_DELTA) {
        return;
    }
    free(frame->payload.fkv_delta.data);
    frame->payload.fkv_delta.data = NULL;
    frame->payload.fkv_delta.data_len = 0;
    frame->payload.fkv_delta.compressed_size = 0;
    frame->payload.fkv_delta.raw_size = 0;
    frame->payload.fkv_delta.entry_count = 0;
    frame->payload.fkv_delta.checksum = 0;
}
