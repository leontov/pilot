/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#ifndef KOLIBRI_UTIL_KEY_MANAGER_H
#define KOLIBRI_UTIL_KEY_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

typedef struct key_file_s {
    char path[512];
    uint32_t rotation_interval_sec;
    unsigned char *data;
    size_t data_len;
    time_t last_loaded;
    time_t last_mtime;
    pthread_mutex_t lock;
} key_file_t;

int key_file_init(key_file_t *kf, const char *path, uint32_t rotation_interval_sec);
int key_file_get(key_file_t *kf, const unsigned char **data, size_t *len);
void key_file_deinit(key_file_t *kf);

#endif /* KOLIBRI_UTIL_KEY_MANAGER_H */
