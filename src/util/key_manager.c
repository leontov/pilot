/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "util/key_manager.h"

#include "util/log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int load_file_locked(key_file_t *kf) {
    struct stat st = {0};
    if (stat(kf->path, &st) != 0) {
        log_error("key_manager: failed to stat %s: %s", kf->path, strerror(errno));
        return -1;
    }

    FILE *fp = fopen(kf->path, "rb");
    if (!fp) {
        log_error("key_manager: failed to open %s: %s", kf->path, strerror(errno));
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }

    unsigned char *buffer = malloc((size_t)len + 1);
    if (!buffer) {
        fclose(fp);
        return -1;
    }
    size_t read = fread(buffer, 1, (size_t)len, fp);
    fclose(fp);
    buffer[read] = '\0';

    free(kf->data);
    kf->data = buffer;
    kf->data_len = read;
    kf->last_loaded = time(NULL);
    kf->last_mtime = st.st_mtime;
    return 0;
}

int key_file_init(key_file_t *kf, const char *path, uint32_t rotation_interval_sec) {
    if (!kf || !path || path[0] == '\0') {
        return -1;
    }
    memset(kf, 0, sizeof(*kf));
    strncpy(kf->path, path, sizeof(kf->path) - 1);
    kf->rotation_interval_sec = rotation_interval_sec;
    pthread_mutex_init(&kf->lock, NULL);
    return 0;
}

int key_file_get(key_file_t *kf, const unsigned char **data, size_t *len) {
    if (!kf) {
        return -1;
    }
    pthread_mutex_lock(&kf->lock);
    int rc = 0;
    time_t now = time(NULL);
    struct stat st = {0};
    if (kf->path[0] && stat(kf->path, &st) != 0) {
        log_error("key_manager: stat failed for %s: %s", kf->path, strerror(errno));
        rc = -1;
        goto out;
    }
    if (!kf->data ||
        (kf->rotation_interval_sec > 0 && now - kf->last_loaded >= (time_t)kf->rotation_interval_sec) ||
        (kf->data && st.st_mtime != kf->last_mtime)) {
        if (load_file_locked(kf) != 0) {
            rc = -1;
            goto out;
        }
    }
    if (data) {
        *data = kf->data;
    }
    if (len) {
        *len = kf->data_len;
    }
out:
    pthread_mutex_unlock(&kf->lock);
    return rc;
}

void key_file_deinit(key_file_t *kf) {
    if (!kf) {
        return;
    }
    pthread_mutex_lock(&kf->lock);
    free(kf->data);
    kf->data = NULL;
    kf->data_len = 0;
    pthread_mutex_unlock(&kf->lock);
    pthread_mutex_destroy(&kf->lock);
}
