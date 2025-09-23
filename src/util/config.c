#include "util/config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_defaults(kolibri_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->http.host, "0.0.0.0", sizeof(cfg->http.host) - 1);
    cfg->http.port = 9000;
    cfg->http.max_body_size = 1024 * 1024;
    cfg->vm.max_steps = 2048;
    cfg->vm.max_stack = 128;
    cfg->vm.trace_depth = 64;
    cfg->seed = 1337;
}

static void strip_comments(char *buf) {
    char *src = buf;
    char *dst = buf;
    while (*src) {
        if (src[0] == '/' && src[1] == '/') {
            while (*src && *src != '\n') {
                src++;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void parse_string(const char *buf, const char *key, char *out, size_t out_len) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(buf, pattern);
    if (!pos) {
        return;
    }
    pos = strchr(pos + strlen(pattern), '"');
    if (!pos) {
        return;
    }
    pos++;
    const char *end = strchr(pos, '"');
    if (!end) {
        return;
    }
    size_t len = (size_t)(end - pos);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, pos, len);
    out[len] = '\0';
}

static void parse_uint(const char *buf, const char *key, uint32_t *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *pos = strstr(buf, pattern);
    if (!pos) {
        return;
    }
    pos = strchr(pos + strlen(pattern), ':');
    if (!pos) {
        return;
    }
    pos++;
    while (*pos == ' ' || *pos == '\t') {
        pos++;
    }
    *out = (uint32_t)strtoul(pos, NULL, 10);
}

int config_load(const char *path, kolibri_config_t *cfg) {
    if (!cfg) {
        errno = EINVAL;
        return -1;
    }
    set_defaults(cfg);

    FILE *fp = fopen(path, "r");
    if (!fp) {
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
    char *buf = calloc(1, (size_t)len + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    size_t read = fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    buf[read] = '\0';

    strip_comments(buf);

    parse_string(buf, "host", cfg->http.host, sizeof(cfg->http.host));
    parse_uint(buf, "port", (uint32_t *)&cfg->http.port);
    parse_uint(buf, "max_body_size", &cfg->http.max_body_size);
    parse_uint(buf, "max_steps", &cfg->vm.max_steps);
    parse_uint(buf, "max_stack", &cfg->vm.max_stack);
    parse_uint(buf, "trace_depth", &cfg->vm.trace_depth);
    parse_uint(buf, "seed", &cfg->seed);

    free(buf);
    return 0;
}
