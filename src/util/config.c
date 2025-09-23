#include "util/config.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_defaults(kolibri_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->http.host, "0.0.0.0", sizeof(cfg->http.host) - 1);
    cfg->http.host[sizeof(cfg->http.host) - 1] = '\0';
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
    bool in_string = false;

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

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

static int parse_string_internal(const char **p, char *out, size_t out_size, bool store) {
    if (**p != '"') {
        return -1;
    }
    (*p)++;
    size_t len = 0;

    while (**p) {
        char ch = *(*p)++;
        if (ch == '"') {
            if (store) {
                if (len >= out_size) {
                    return -1;
                }
                out[len] = '\0';
            }
            return 0;
        }
        if (ch == '\\') {
            char esc = **p;
            if (esc == '\0') {
                return -1;
            }
            (*p)++;
            switch (esc) {
            case '"':
            case '\\':
            case '/':
                ch = esc;
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            default:
                return -1;
            }
        }
        if (store) {
            if (len + 1 >= out_size) {
                return -1;
            }
            out[len++] = ch;
        }
    }
    return -1;
}

static int parse_string_token(const char **p, char *out, size_t out_size) {
    return parse_string_internal(p, out, out_size, true);
}

static int skip_string(const char **p) {
    return parse_string_internal(p, NULL, 0, false);
}

static int parse_uint64_token(const char **p, uint64_t *out) {
    if (**p == '-') {
        return -1;
    }
    if (!isdigit((unsigned char)**p)) {
        return -1;
    }
    uint64_t value = 0;
    while (isdigit((unsigned char)**p)) {
        unsigned int digit = (unsigned int)(*(*p)++ - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return -1;
        }
        value = value * 10 + digit;
    }
    *out = value;
    return 0;
}

static int skip_value(const char **p);

static int skip_object(const char **p) {
    if (**p != '{') {
        return -1;
    }
    (*p)++;
    skip_ws(p);
    if (**p == '}') {
        (*p)++;
        return 0;
    }
    while (**p) {
        if (skip_string(p) != 0) {
            return -1;
        }
        skip_ws(p);
        if (**p != ':') {
            return -1;
        }
        (*p)++;
        skip_ws(p);
        if (skip_value(p) != 0) {
            return -1;
        }
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            skip_ws(p);
            continue;
        }
        if (**p == '}') {
            (*p)++;
            return 0;
        }
        return -1;
    }
    return -1;
}

static int skip_array(const char **p) {
    if (**p != '[') {
        return -1;
    }
    (*p)++;
    skip_ws(p);
    if (**p == ']') {
        (*p)++;
        return 0;
    }
    while (**p) {
        if (skip_value(p) != 0) {
            return -1;
        }
        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            skip_ws(p);
            continue;
        }
        if (**p == ']') {
            (*p)++;
            return 0;
        }
        return -1;
    }
    return -1;
}

static int skip_literal(const char **p, const char *literal) {
    size_t len = strlen(literal);
    if (strncmp(*p, literal, len) != 0) {
        return -1;
    }
    *p += len;
    return 0;
}

static int skip_value(const char **p) {
    if (**p == '"') {
        return skip_string(p);
    }
    if (**p == '{') {
        return skip_object(p);
    }
    if (**p == '[') {
        return skip_array(p);
    }
    if (**p == '-' || isdigit((unsigned char)**p)) {
        if (**p == '-') {
            (*p)++;
            if (!isdigit((unsigned char)**p)) {
                return -1;
            }
        }
        while (isdigit((unsigned char)**p)) {
            (*p)++;
        }
        if (**p == '.') {
            (*p)++;
            if (!isdigit((unsigned char)**p)) {
                return -1;
            }
            while (isdigit((unsigned char)**p)) {
                (*p)++;
            }
        }
        if (**p == 'e' || **p == 'E') {
            (*p)++;
            if (**p == '+' || **p == '-') {
                (*p)++;
            }
            if (!isdigit((unsigned char)**p)) {
                return -1;
            }
            while (isdigit((unsigned char)**p)) {
                (*p)++;
            }
        }
        return 0;
    }
    if (skip_literal(p, "true") == 0 || skip_literal(p, "false") == 0 ||
        skip_literal(p, "null") == 0) {
        return 0;
    }
    return -1;
}

static int parse_http_object(const char **p, kolibri_config_t *cfg) {
    if (**p != '{') {
        return -1;
    }
    (*p)++;
    bool seen_host = false;
    bool seen_port = false;

    while (**p) {
        skip_ws(p);
        if (**p == '}') {
            (*p)++;
            break;
        }
        char key[32];
        if (parse_string_token(p, key, sizeof(key)) != 0) {
            return -1;
        }
        skip_ws(p);
        if (**p != ':') {
            return -1;
        }
        (*p)++;
        skip_ws(p);

        if (strcmp(key, "host") == 0) {
            if (parse_string_token(p, cfg->http.host, sizeof(cfg->http.host)) != 0) {
                return -1;
            }
            seen_host = true;
        } else if (strcmp(key, "port") == 0) {
            uint64_t value = 0;
            if (parse_uint64_token(p, &value) != 0 || value > UINT16_MAX) {
                return -1;
            }
            cfg->http.port = (uint16_t)value;
            seen_port = true;
        } else if (strcmp(key, "max_body_size") == 0) {
            uint64_t value = 0;
            if (parse_uint64_token(p, &value) != 0 || value > UINT32_MAX) {
                return -1;
            }
            cfg->http.max_body_size = (uint32_t)value;
        } else {
            if (skip_value(p) != 0) {
                return -1;
            }
        }

        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == '}') {
            (*p)++;
            break;
        }
        return -1;
    }

    if (!seen_host || !seen_port) {
        return -1;
    }
    return 0;
}

static int parse_vm_object(const char **p, kolibri_config_t *cfg) {
    if (**p != '{') {
        return -1;
    }
    (*p)++;
    bool seen_max_steps = false;
    bool seen_max_stack = false;
    bool seen_trace_depth = false;

    while (**p) {
        skip_ws(p);
        if (**p == '}') {
            (*p)++;
            break;
        }
        char key[32];
        if (parse_string_token(p, key, sizeof(key)) != 0) {
            return -1;
        }
        skip_ws(p);
        if (**p != ':') {
            return -1;
        }
        (*p)++;
        skip_ws(p);

        if (strcmp(key, "max_steps") == 0) {
            uint64_t value = 0;
            if (parse_uint64_token(p, &value) != 0 || value > UINT32_MAX) {
                return -1;
            }
            cfg->vm.max_steps = (uint32_t)value;
            seen_max_steps = true;
        } else if (strcmp(key, "max_stack") == 0) {
            uint64_t value = 0;
            if (parse_uint64_token(p, &value) != 0 || value > UINT32_MAX) {
                return -1;
            }
            cfg->vm.max_stack = (uint32_t)value;
            seen_max_stack = true;
        } else if (strcmp(key, "trace_depth") == 0) {
            uint64_t value = 0;
            if (parse_uint64_token(p, &value) != 0 || value > UINT32_MAX) {
                return -1;
            }
            cfg->vm.trace_depth = (uint32_t)value;
            seen_trace_depth = true;
        } else {
            if (skip_value(p) != 0) {
                return -1;
            }
        }

        skip_ws(p);
        if (**p == ',') {
            (*p)++;
            continue;
        }
        if (**p == '}') {
            (*p)++;
            break;
        }
        return -1;
    }

    if (!seen_max_steps || !seen_max_stack || !seen_trace_depth) {
        return -1;
    }
    return 0;
}

int config_load(const char *path, kolibri_config_t *cfg) {
    if (!cfg) {
        errno = EINVAL;
        return -1;
    }
    set_defaults(cfg);
    kolibri_config_t tmp = *cfg;

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

    const char *p = buf;
    skip_ws(&p);
    if (*p != '{') {
        free(buf);
        errno = EINVAL;
        return -1;
    }
    p++;

    bool seen_http = false;
    bool seen_vm = false;
    bool seen_seed = false;

    while (*p) {
        skip_ws(&p);
        if (*p == '}') {
            p++;
            break;
        }
        char key[32];
        if (parse_string_token(&p, key, sizeof(key)) != 0) {
            errno = EINVAL;
            free(buf);
            return -1;
        }
        skip_ws(&p);
        if (*p != ':') {
            errno = EINVAL;
            free(buf);
            return -1;
        }
        p++;
        skip_ws(&p);

        if (strcmp(key, "http") == 0) {
            if (parse_http_object(&p, &tmp) != 0) {
                errno = EINVAL;
                free(buf);
                return -1;
            }
            seen_http = true;
        } else if (strcmp(key, "vm") == 0) {
            if (parse_vm_object(&p, &tmp) != 0) {
                errno = EINVAL;
                free(buf);
                return -1;
            }
            seen_vm = true;
        } else if (strcmp(key, "seed") == 0) {
            uint64_t value = 0;
            if (parse_uint64_token(&p, &value) != 0 || value > UINT32_MAX) {
                errno = EINVAL;
                free(buf);
                return -1;
            }
            tmp.seed = (uint32_t)value;
            seen_seed = true;
        } else {
            if (skip_value(&p) != 0) {
                errno = EINVAL;
                free(buf);
                return -1;
            }
        }

        skip_ws(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            p++;
            break;
        }
        errno = EINVAL;
        free(buf);
        return -1;
    }

    skip_ws(&p);
    if (*p != '\0' || !seen_http || !seen_vm || !seen_seed) {
        errno = EINVAL;
        free(buf);
        return -1;
    }

    *cfg = tmp;
    free(buf);
    return 0;
}
