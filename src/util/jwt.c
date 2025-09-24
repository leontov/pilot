/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "util/jwt.h"

#include "util/log.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned char g_base64_table[256];
static pthread_once_t g_base64_once = PTHREAD_ONCE_INIT;

static void base64_table_init(void) {
    for (size_t i = 0; i < sizeof(g_base64_table); ++i) {
        g_base64_table[i] = 0x80;
    }
    for (int i = 0; i < 26; ++i) {
        g_base64_table[(unsigned char)('A' + i)] = (unsigned char)i;
        g_base64_table[(unsigned char)('a' + i)] = (unsigned char)(26 + i);
    }
    for (int i = 0; i < 10; ++i) {
        g_base64_table[(unsigned char)('0' + i)] = (unsigned char)(52 + i);
    }
    g_base64_table[(unsigned char)'+'] = 62;
    g_base64_table[(unsigned char)'/'] = 63;
    g_base64_table[(unsigned char)'='] = 0;
}

static char *dup_slice(const char *src, size_t len) {
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len);
    copy[len] = '\0';
    return copy;
}

static int base64url_to_base64(const char *src, char **dst_out) {
    size_t len = strlen(src);
    size_t pad = (4 - (len % 4)) % 4;
    size_t out_len = len + pad;
    char *dst = malloc(out_len + 1);
    if (!dst) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        char c = src[i];
        if (c == '-') {
            dst[i] = '+';
        } else if (c == '_') {
            dst[i] = '/';
        } else {
            dst[i] = c;
        }
    }
    for (size_t i = 0; i < pad; ++i) {
        dst[len + i] = '=';
    }
    dst[out_len] = '\0';
    *dst_out = dst;
    return 0;
}

static int base64_decode(const char *input, unsigned char **out, size_t *out_len) {
    pthread_once(&g_base64_once, base64_table_init);

    size_t len = strlen(input);
    if (len % 4 != 0) {
        return -1;
    }
    size_t output_len = (len / 4) * 3;
    if (len >= 1 && input[len - 1] == '=') {
        output_len--;
    }
    if (len >= 2 && input[len - 2] == '=') {
        output_len--;
    }
    unsigned char *buffer = malloc(output_len + 1);
    if (!buffer) {
        return -1;
    }
    size_t out_idx = 0;
    for (size_t i = 0; i < len; i += 4) {
        unsigned char v0 = g_base64_table[(unsigned char)input[i]];
        unsigned char v1 = g_base64_table[(unsigned char)input[i + 1]];
        unsigned char v2 = g_base64_table[(unsigned char)input[i + 2]];
        unsigned char v3 = g_base64_table[(unsigned char)input[i + 3]];
        if ((v0 | v1 | v2 | v3) & 0x80) {
            free(buffer);
            return -1;
        }
        unsigned triple = (v0 << 18) | (v1 << 12) | (v2 << 6) | v3;
        buffer[out_idx++] = (unsigned char)((triple >> 16) & 0xFF);
        if (input[i + 2] != '=') {
            buffer[out_idx++] = (unsigned char)((triple >> 8) & 0xFF);
        }
        if (input[i + 3] != '=') {
            buffer[out_idx++] = (unsigned char)(triple & 0xFF);
        }
    }
    buffer[out_idx] = '\0';
    if (out) {
        *out = buffer;
    } else {
        free(buffer);
        return -1;
    }
    if (out_len) {
        *out_len = out_idx;
    }
    return 0;
}

static int base64url_decode_alloc(const char *input, unsigned char **out, size_t *out_len) {
    char *tmp = NULL;
    if (base64url_to_base64(input, &tmp) != 0) {
        return -1;
    }
    int rc = base64_decode(tmp, out, out_len);
    free(tmp);
    return rc;
}

static int extract_json_string(const char *json, const char *key, char *out, size_t out_size) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *found = strstr(json, pattern);
    if (!found) {
        return -1;
    }
    found += strlen(pattern);
    size_t idx = 0;
    while (found[idx] && found[idx] != '\"') {
        if (idx + 1 >= out_size) {
            return -1;
        }
        out[idx] = found[idx];
        idx++;
    }
    if (found[idx] != '\"') {
        return -1;
    }
    out[idx] = '\0';
    return 0;
}

static int extract_json_uint64(const char *json, const char *key, uint64_t *value) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *found = strstr(json, pattern);
    if (!found) {
        return -1;
    }
    found += strlen(pattern);
    while (*found && isspace((unsigned char)*found)) {
        found++;
    }
    char *endptr = NULL;
    unsigned long long tmp = strtoull(found, &endptr, 10);
    if (endptr == found) {
        return -1;
    }
    if (value) {
        *value = (uint64_t)tmp;
    }
    return 0;
}

static int secure_compare(const unsigned char *a, const unsigned char *b, size_t len) {
    unsigned char diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= (unsigned char)(a[i] ^ b[i]);
    }
    return diff == 0 ? 0 : -1;
}

int jwt_verify_hs256(const char *token,
                     const unsigned char *secret,
                     size_t secret_len,
                     const char *expected_issuer,
                     const char *expected_audience,
                     jwt_claims_t *claims_out) {
    if (!token || !secret || secret_len == 0) {
        return -1;
    }
    const char *dot1 = strchr(token, '.');
    if (!dot1) {
        return -1;
    }
    const char *dot2 = strchr(dot1 + 1, '.');
    if (!dot2) {
        return -1;
    }
    size_t header_len = (size_t)(dot1 - token);
    size_t payload_len = (size_t)(dot2 - dot1 - 1);
    size_t signing_len = (size_t)(dot2 - token);

    char *header_b64 = dup_slice(token, header_len);
    char *payload_b64 = dup_slice(dot1 + 1, payload_len);
    if (!header_b64 || !payload_b64) {
        free(header_b64);
        free(payload_b64);
        return -1;
    }

    unsigned char *header = NULL;
    size_t header_dec_len = 0;
    if (base64url_decode_alloc(header_b64, &header, &header_dec_len) != 0) {
        free(header_b64);
        free(payload_b64);
        return -1;
    }
    header[header_dec_len] = '\0';
    if (!strstr((const char *)header, "\"alg\":\"HS256\"")) {
        free(header_b64);
        free(payload_b64);
        free(header);
        return -1;
    }

    unsigned char *payload = NULL;
    size_t payload_dec_len = 0;
    if (base64url_decode_alloc(payload_b64, &payload, &payload_dec_len) != 0) {
        free(header_b64);
        free(payload_b64);
        free(header);
        return -1;
    }
    payload[payload_dec_len] = '\0';

    const unsigned char *signature = NULL;
    size_t signature_len = 0;
    if (base64url_decode_alloc(dot2 + 1, (unsigned char **)&signature, &signature_len) != 0) {
        free(header_b64);
        free(payload_b64);
        free(header);
        free(payload);
        return -1;
    }

    unsigned char digest[SHA256_DIGEST_LENGTH];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(), secret, (int)secret_len, (const unsigned char *)token, signing_len, digest, &digest_len);
    if (digest_len != signature_len || secure_compare(digest, signature, signature_len) != 0) {
        free(header_b64);
        free(payload_b64);
        free(header);
        free(payload);
        free((void *)signature);
        return -1;
    }

    jwt_claims_t claims = {0};
    if (extract_json_string((const char *)payload, "iss", claims.issuer, sizeof(claims.issuer)) != 0) {
        log_error("jwt: issuer missing");
        goto fail;
    }
    if (extract_json_string((const char *)payload, "aud", claims.audience, sizeof(claims.audience)) != 0) {
        log_error("jwt: audience missing");
        goto fail;
    }
    extract_json_string((const char *)payload, "sub", claims.subject, sizeof(claims.subject));
    extract_json_uint64((const char *)payload, "iat", &claims.issued_at);
    if (extract_json_uint64((const char *)payload, "exp", &claims.expires_at) != 0) {
        log_error("jwt: expiration missing");
        goto fail;
    }

    time_t now = time(NULL);
    if (claims.expires_at && now > (time_t)claims.expires_at) {
        log_error("jwt: token expired");
        goto fail;
    }
    if (expected_issuer && expected_issuer[0] && strcmp(expected_issuer, claims.issuer) != 0) {
        log_error("jwt: issuer mismatch");
        goto fail;
    }
    if (expected_audience && expected_audience[0] && strcmp(expected_audience, claims.audience) != 0) {
        log_error("jwt: audience mismatch");
        goto fail;
    }

    if (claims_out) {
        *claims_out = claims;
    }

    free(header_b64);
    free(payload_b64);
    free(header);
    free(payload);
    free((void *)signature);
    return 0;

fail:
    free(header_b64);
    free(payload_b64);
    free(header);
    free(payload);
    free((void *)signature);
    return -1;
}
