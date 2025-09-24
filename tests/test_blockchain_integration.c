#include "blockchain.h"
#include "protocol/swarm.h"

#include <assert.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned char priv[32];
    EVP_PKEY *pkey;
    unsigned char pub[32];
    size_t pub_len;
} Ed25519Keypair;

static void init_formula(Formula *formula, const char *id, const char *content, double effectiveness) {
    memset(formula, 0, sizeof(*formula));
    strncpy(formula->id, id, sizeof(formula->id) - 1);
    strncpy(formula->content, content, sizeof(formula->content) - 1);
    formula->representation = FORMULA_REPRESENTATION_TEXT;
    formula->effectiveness = effectiveness;
}

static void keypair_init(Ed25519Keypair *pair) {
    assert(pair);
    for (size_t i = 0; i < sizeof(pair->priv); ++i) {
        pair->priv[i] = (unsigned char)(i + 1);
    }
    pair->pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, pair->priv, sizeof(pair->priv));
    assert(pair->pkey);
    pair->pub_len = sizeof(pair->pub);
    int rc = EVP_PKEY_get_raw_public_key(pair->pkey, pair->pub, &pair->pub_len);
    assert(rc == 1);
}

static void keypair_cleanup(Ed25519Keypair *pair) {
    if (pair && pair->pkey) {
        EVP_PKEY_free(pair->pkey);
        pair->pkey = NULL;
    }
}

static size_t make_offer_message(const SwarmBlockOfferPayload *offer, char *buf, size_t buf_len) {
    int written = snprintf(buf,
                           buf_len,
                           "%.*s|%u|%u|%u",
                           SWARM_BLOCK_ID_DIGITS,
                           offer->block_id,
                           offer->height,
                           offer->poe_milli,
                           offer->program_count);
    assert(written > 0);
    assert((size_t)written < buf_len);
    return (size_t)written;
}

static void sign_offer(const char *message, size_t message_len, const Ed25519Keypair *pair, unsigned char *sig_out, size_t *sig_len) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    assert(ctx);
    int rc = EVP_DigestSignInit(ctx, NULL, NULL, NULL, pair->pkey);
    assert(rc == 1);
    *sig_len = 64;
    rc = EVP_DigestSign(ctx, sig_out, sig_len, (const unsigned char *)message, message_len);
    assert(rc == 1);
    EVP_MD_CTX_free(ctx);
}

static unsigned int hmac_offer(const char *message, size_t len, const unsigned char *key, size_t key_len, unsigned char *mac_out) {
    unsigned int mac_len = 0;
    unsigned char *mac = HMAC(EVP_sha256(), key, (int)key_len, (const unsigned char *)message, len, mac_out, &mac_len);
    assert(mac == mac_out);
    return mac_len;
}

static void test_swarm_rejects_weak_block(void) {
    Blockchain *chain = blockchain_create();
    assert(chain);
    blockchain_set_policy(chain, 0.7, 48.0);

    SwarmPeerState peer;
    swarm_peer_state_init(&peer, 0);
    SwarmBlockchainLink link;
    swarm_blockchain_link_init(&link, chain, &peer);

    Ed25519Keypair keys;
    keypair_init(&keys);
    assert(swarm_blockchain_link_set_ed25519_key(&link, keys.pub, keys.pub_len) == 0);
    const unsigned char hmac_key[] = "0123456789ABCDEF";
    assert(swarm_blockchain_link_set_hmac_key(&link, hmac_key, sizeof(hmac_key) - 1) == 0);

    Formula strong;
    init_formula(&strong, "strong", "alpha", 0.92);
    Formula *good_formulas[] = {&strong};
    BlockchainBlockSpec strong_spec = {.formulas = good_formulas, .formula_count = 1};
    SwarmBlockOfferPayload good_offer = {0};
    memcpy(good_offer.block_id, "0000000000000001", SWARM_BLOCK_ID_DIGITS);
    good_offer.block_id[SWARM_BLOCK_ID_DIGITS] = '\0';
    good_offer.height = 1;
    good_offer.poe_milli = 920;
    good_offer.program_count = 1;

    char message[128];
    size_t message_len = make_offer_message(&good_offer, message, sizeof(message));
    unsigned char signature[64];
    size_t signature_len = 0;
    sign_offer(message, message_len, &keys, signature, &signature_len);
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int mac_len = hmac_offer(message, message_len, hmac_key, sizeof(hmac_key) - 1, mac);

    BlockValidationStatus status = BLOCK_VALIDATION_PENDING;
    assert(swarm_blockchain_link_process_offer(&link,
                                               &good_offer,
                                               &strong_spec,
                                               signature,
                                               signature_len,
                                               mac,
                                               mac_len,
                                               &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);
    assert(blockchain_height(chain) == 1);

    Formula weak;
    init_formula(&weak, "weak", "beta", 0.25);
    Formula *weak_formulas[] = {&weak};
    BlockchainBlockSpec weak_spec = {
        .formulas = weak_formulas,
        .formula_count = 1,
        .prev_hash = blockchain_get_last_hash(chain),
    };
    SwarmBlockOfferPayload weak_offer = {0};
    memcpy(weak_offer.block_id, "0000000000000002", SWARM_BLOCK_ID_DIGITS);
    weak_offer.block_id[SWARM_BLOCK_ID_DIGITS] = '\0';
    weak_offer.height = 2;
    weak_offer.poe_milli = 250;
    weak_offer.program_count = 1;

    message_len = make_offer_message(&weak_offer, message, sizeof(message));
    sign_offer(message, message_len, &keys, signature, &signature_len);
    mac_len = hmac_offer(message, message_len, hmac_key, sizeof(hmac_key) - 1, mac);

    status = BLOCK_VALIDATION_PENDING;
    assert(!swarm_blockchain_link_process_offer(&link,
                                                &weak_offer,
                                                &weak_spec,
                                                signature,
                                                signature_len,
                                                mac,
                                                mac_len,
                                                &status));
    assert(status == BLOCK_VALIDATION_REJECTED);
    assert(blockchain_height(chain) == 1);

    keypair_cleanup(&keys);
    blockchain_destroy(chain);
}

static void test_chain_recovers_with_stronger_branch(void) {
    Blockchain *chain = blockchain_create();
    assert(chain);
    blockchain_set_policy(chain, 0.6, 64.0);

    SwarmBlockchainLink link;
    SwarmPeerState peer;
    swarm_peer_state_init(&peer, 0);
    swarm_blockchain_link_init(&link, chain, &peer);

    Ed25519Keypair keys;
    keypair_init(&keys);
    assert(swarm_blockchain_link_set_ed25519_key(&link, keys.pub, keys.pub_len) == 0);
    const unsigned char hmac_key[] = "0123456789ABCDEF";
    assert(swarm_blockchain_link_set_hmac_key(&link, hmac_key, sizeof(hmac_key) - 1) == 0);

    Formula base;
    init_formula(&base, "base", "anchor", 0.85);
    Formula *base_formulas[] = {&base};
    BlockchainBlockSpec base_spec = {.formulas = base_formulas, .formula_count = 1};
    SwarmBlockOfferPayload base_offer = {0};
    memcpy(base_offer.block_id, "0000000000000100", SWARM_BLOCK_ID_DIGITS);
    base_offer.block_id[SWARM_BLOCK_ID_DIGITS] = '\0';
    base_offer.height = 1;
    base_offer.poe_milli = 850;
    base_offer.program_count = 1;

    char message[128];
    size_t message_len = make_offer_message(&base_offer, message, sizeof(message));
    unsigned char signature[64];
    size_t signature_len = 0;
    sign_offer(message, message_len, &keys, signature, &signature_len);
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int mac_len = hmac_offer(message, message_len, hmac_key, sizeof(hmac_key) - 1, mac);
    BlockValidationStatus status = BLOCK_VALIDATION_PENDING;
    assert(swarm_blockchain_link_process_offer(&link,
                                               &base_offer,
                                               &base_spec,
                                               signature,
                                               signature_len,
                                               mac,
                                               mac_len,
                                               &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);
    char anchor_hash[65];
    strncpy(anchor_hash, blockchain_get_last_hash(chain), sizeof(anchor_hash));
    anchor_hash[sizeof(anchor_hash) - 1] = '\0';

    Formula branch_a;
    init_formula(&branch_a, "branch_a", "slow", 0.68);
    Formula *branch_a_formulas[] = {&branch_a};
    BlockchainBlockSpec branch_a_spec = {
        .formulas = branch_a_formulas,
        .formula_count = 1,
        .prev_hash = anchor_hash,
    };
    SwarmBlockOfferPayload branch_a_offer = {0};
    memcpy(branch_a_offer.block_id, "0000000000000101", SWARM_BLOCK_ID_DIGITS);
    branch_a_offer.block_id[SWARM_BLOCK_ID_DIGITS] = '\0';
    branch_a_offer.height = 2;
    branch_a_offer.poe_milli = 680;
    branch_a_offer.program_count = 1;
    message_len = make_offer_message(&branch_a_offer, message, sizeof(message));
    sign_offer(message, message_len, &keys, signature, &signature_len);
    mac_len = hmac_offer(message, message_len, hmac_key, sizeof(hmac_key) - 1, mac);
    status = BLOCK_VALIDATION_PENDING;
    assert(swarm_blockchain_link_process_offer(&link,
                                               &branch_a_offer,
                                               &branch_a_spec,
                                               signature,
                                               signature_len,
                                               mac,
                                               mac_len,
                                               &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);
    char branch_a_hash[65];
    strncpy(branch_a_hash, blockchain_get_last_hash(chain), sizeof(branch_a_hash));
    branch_a_hash[sizeof(branch_a_hash) - 1] = '\0';

    Formula branch_b;
    init_formula(&branch_b, "branch_b", "fast", 0.95);
    Formula *branch_b_formulas[] = {&branch_b};
    BlockchainBlockSpec branch_b_spec = {
        .formulas = branch_b_formulas,
        .formula_count = 1,
        .prev_hash = anchor_hash,
    };
    SwarmBlockOfferPayload branch_b_offer = {0};
    memcpy(branch_b_offer.block_id, "0000000000000102", SWARM_BLOCK_ID_DIGITS);
    branch_b_offer.block_id[SWARM_BLOCK_ID_DIGITS] = '\0';
    branch_b_offer.height = 2;
    branch_b_offer.poe_milli = 950;
    branch_b_offer.program_count = 1;
    message_len = make_offer_message(&branch_b_offer, message, sizeof(message));
    sign_offer(message, message_len, &keys, signature, &signature_len);
    mac_len = hmac_offer(message, message_len, hmac_key, sizeof(hmac_key) - 1, mac);
    status = BLOCK_VALIDATION_PENDING;
    assert(swarm_blockchain_link_process_offer(&link,
                                               &branch_b_offer,
                                               &branch_b_spec,
                                               signature,
                                               signature_len,
                                               mac,
                                               mac_len,
                                               &status));
    assert(status == BLOCK_VALIDATION_ACCEPTED);

    const char *tip_hash = blockchain_get_last_hash(chain);
    const Block *tip = blockchain_find_block(chain, tip_hash);
    assert(tip);
    assert(tip->poe_average > 0.9);
    assert(tip->on_main_chain);
    const Block *loser = blockchain_find_block(chain, branch_a_hash);
    assert(loser);
    assert(!loser->on_main_chain);

    keypair_cleanup(&keys);
    blockchain_destroy(chain);
}

int main(void) {
    test_swarm_rejects_weak_block();
    test_chain_recovers_with_stronger_branch();
    printf("blockchain integration tests passed\n");
    return 0;
}
