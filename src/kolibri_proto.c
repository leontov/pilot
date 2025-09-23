#include "kolibri_proto.h"
#include <openssl/hmac.h>
#include <string.h>

int verify_message(const char* message, size_t message_len, const char* hmac, const char* key) {
    unsigned char computed_hmac[32];
    unsigned int hmac_len;
    
    HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)message, message_len,
         computed_hmac, &hmac_len);
    
    return (memcmp(hmac, computed_hmac, 32) == 0);
}

int create_message(const char* payload, size_t payload_len, const char* key,
                  char* buffer, size_t buffer_size) {
    if (buffer_size < payload_len + 32) {
        return -1;
    }

    unsigned char hmac[32];
    unsigned int hmac_len;
    
    HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)payload, payload_len,
         hmac, &hmac_len);
    
    memcpy(buffer, hmac, 32);
    memcpy(buffer + 32, payload, payload_len);
    
    return payload_len + 32;
}
