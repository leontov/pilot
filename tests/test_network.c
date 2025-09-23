#include "network.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char* data;
} ReceiveContext;

static void* receiver_thread(void* arg) {
    ReceiveContext* ctx = (ReceiveContext*)arg;
    for (int i = 0; i < 50; ++i) {
        ctx->data = network_receive_data();
        if (ctx->data) {
            break;
        }
        usleep(100000);
    }
    return NULL;
}

int main(void) {
    const int port = 19090;
    if (!network_init(port)) {
        fprintf(stderr, "failed to init network\n");
        return 1;
    }

    ReceiveContext ctx = {0};
    pthread_t thread;
    if (pthread_create(&thread, NULL, receiver_thread, &ctx) != 0) {
        fprintf(stderr, "failed to create thread\n");
        network_cleanup();
        return 1;
    }

    usleep(200000);

    const char* payload = "{\"type\":\"federated_update\",\"payload\":{\"value\":42}}";
    bool sent = network_send_data("127.0.0.1", port, payload);
    pthread_join(thread, NULL);

    bool success = sent && ctx.data && strcmp(ctx.data, payload) == 0;
    if (ctx.data) {
        free(ctx.data);
    }

    network_cleanup();

    if (!success) {
        fprintf(stderr, "network integration failed\n");
        return 1;
    }

    printf("network integration success\n");
    return 0;
}
