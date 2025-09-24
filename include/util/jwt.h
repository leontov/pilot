/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_UTIL_JWT_H
#define KOLIBRI_UTIL_JWT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char issuer[128];
    char subject[128];
    char audience[128];
    uint64_t issued_at;
    uint64_t expires_at;
} jwt_claims_t;

int jwt_verify_hs256(const char *token,
                     const unsigned char *secret,
                     size_t secret_len,
                     const char *expected_issuer,
                     const char *expected_audience,
                     jwt_claims_t *claims_out);

#endif /* KOLIBRI_UTIL_JWT_H */
