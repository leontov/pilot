#include "util/config.h"

#include <errno.h>
#include <json-c/json.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_defaults(kolibri_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->http.host, "0.0.0.0", sizeof(cfg->http.host) - 1);
    cfg->http.port = 9000;
    cfg->vm.max_steps = 2048;
    cfg->vm.max_stack = 128;
    cfg->vm.trace_depth = 64;
    cfg->seed = 1337;
}

static void strip_comments(char *buf) {
    char *src = buf;
    char *dst = buf;
    int in_string = 0;
    while (*src) {
        if (!in_string && src[0] == '/' && src[1] == '/') {
            src += 2;
            while (*src && *src != '\n') {
                src++;
            }
            continue;
        }
        if (!in_string && src[0] == '/' && src[1] == '*') {
            src += 2;
            while (src[0] && !(src[0] == '*' && src[1] == '/')) {
                src++;
            }
            if (src[0] == '*' && src[1] == '/') {
                src += 2;
            }
            continue;
        }
        if (*src == '"' && (src == buf || src[-1] != '\\')) {
            in_string = !in_string;
        }
        *dst++ = *src++;
    }
    *dst = '\0';
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

    int rc = -1;
    struct json_object *root = json_tokener_parse(buf);
    if (!root || !json_object_is_type(root, json_type_object)) {
        errno = EINVAL;
        goto cleanup;
    }

    struct json_object *http_obj = NULL;
    struct json_object *vm_obj = NULL;
    struct json_object *value = NULL;

    if (!json_object_object_get_ex(root, "http", &http_obj) ||
        !json_object_is_type(http_obj, json_type_object)) {
        errno = EINVAL;
        goto cleanup;
    }

    if (!json_object_object_get_ex(http_obj, "host", &value) ||
        !json_object_is_type(value, json_type_string)) {
        errno = EINVAL;
        goto cleanup;
    }
    const char *host = json_object_get_string(value);
    strncpy(cfg->http.host, host, sizeof(cfg->http.host) - 1);
    cfg->http.host[sizeof(cfg->http.host) - 1] = '\0';

    if (!json_object_object_get_ex(http_obj, "port", &value) ||
        !json_object_is_type(value, json_type_int)) {
        errno = EINVAL;
        goto cleanup;
    }
    int64_t port = json_object_get_int64(value);
    if (port < 0 || port > UINT16_MAX) {
        errno = EINVAL;
        goto cleanup;
    }
    cfg->http.port = (uint16_t)port;

    if (!json_object_object_get_ex(root, "vm", &vm_obj) ||
        !json_object_is_type(vm_obj, json_type_object)) {
        errno = EINVAL;
        goto cleanup;
    }

    if (!json_object_object_get_ex(vm_obj, "max_steps", &value) ||
        !json_object_is_type(value, json_type_int)) {
        errno = EINVAL;
        goto cleanup;
    }
    int64_t max_steps = json_object_get_int64(value);
    if (max_steps < 0 || max_steps > UINT32_MAX) {
        errno = EINVAL;
        goto cleanup;
    }
    cfg->vm.max_steps = (uint32_t)max_steps;

    if (!json_object_object_get_ex(vm_obj, "max_stack", &value) ||
        !json_object_is_type(value, json_type_int)) {
        errno = EINVAL;
        goto cleanup;
    }
    int64_t max_stack = json_object_get_int64(value);
    if (max_stack < 0 || max_stack > UINT32_MAX) {
        errno = EINVAL;
        goto cleanup;
    }
    cfg->vm.max_stack = (uint32_t)max_stack;

    if (!json_object_object_get_ex(vm_obj, "trace_depth", &value) ||
        !json_object_is_type(value, json_type_int)) {
        errno = EINVAL;
        goto cleanup;
    }
    int64_t trace_depth = json_object_get_int64(value);
    if (trace_depth < 0 || trace_depth > UINT32_MAX) {
        errno = EINVAL;
        goto cleanup;
    }
    cfg->vm.trace_depth = (uint32_t)trace_depth;

    if (!json_object_object_get_ex(root, "seed", &value) ||
        !json_object_is_type(value, json_type_int)) {
        errno = EINVAL;
        goto cleanup;
    }
    int64_t seed = json_object_get_int64(value);
    if (seed < 0 || seed > UINT32_MAX) {
        errno = EINVAL;
        goto cleanup;
    }
    cfg->seed = (uint32_t)seed;

    rc = 0;

cleanup:
    if (root) {
        json_object_put(root);
    }
    free(buf);
    return rc;
}
