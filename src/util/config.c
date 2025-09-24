/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "util/config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *cur;
} json_cursor_t;

static void set_defaults(kolibri_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->http.host, "0.0.0.0", sizeof(cfg->http.host) - 1);
    cfg->http.port = 9000;
    cfg->http.max_body_size = 1024 * 1024;

    cfg->vm.max_steps = 2048;
    cfg->vm.max_stack = 128;
    cfg->vm.trace_depth = 64;

    cfg->fkv.top_k = 4;

    cfg->seed = 1337;

    strncpy(cfg->ai.snapshot_path,
            "data/kolibri_ai_snapshot.json",
            sizeof(cfg->ai.snapshot_path) - 1);
    cfg->ai.snapshot_limit = 2048;

    cfg->selfplay.tasks_per_iteration = 8;
    cfg->selfplay.max_difficulty = 4;

    cfg->search.max_candidates = 16;
    cfg->search.max_terms = 8;
    cfg->search.max_coefficient = 9;
    cfg->search.max_formula_length = 32;
    cfg->search.base_effectiveness = 0.5;
}

static void skip_ws(json_cursor_t *cur) {
    while (*cur->cur && isspace((unsigned char)*cur->cur)) {
        cur->cur++;
    }
}

static int consume_char(json_cursor_t *cur, char expected) {
    skip_ws(cur);
    if (*cur->cur != expected) {
        return -1;
    }
    cur->cur++;
    return 0;
}

static int parse_string(json_cursor_t *cur, char *out, size_t out_size) {
    skip_ws(cur);
    if (*cur->cur != '"') {
        return -1;
    }
    cur->cur++;
    size_t len = 0;
    while (*cur->cur) {
        char ch = *cur->cur++;
        if (ch == '"') {
            if (out && out_size > 0) {
                if (len >= out_size) {
                    return -1;
                }
                out[len] = '\0';
            }
            return 0;
        }
        if (ch == '\\') {
            ch = *cur->cur++;
            switch (ch) {
            case '\\':
            case '"':
            case '/':
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
        if (out && out_size > 0) {
            if (len + 1 >= out_size) {
                return -1;
            }
            out[len++] = ch;
        }
    }
    return -1;
}

static int parse_uint(json_cursor_t *cur, uint64_t *out) {
    skip_ws(cur);
    if (!isdigit((unsigned char)*cur->cur)) {
        return -1;
    }
    uint64_t value = 0;
    while (isdigit((unsigned char)*cur->cur)) {
        unsigned digit = (unsigned)(*cur->cur++ - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return -1;
        }
        value = value * 10 + digit;
    }
    *out = value;
    return 0;
}

static int parse_double(json_cursor_t *cur, double *out) {
    skip_ws(cur);
    const char *start = cur->cur;
    if (*cur->cur == '-') {
        cur->cur++;
    }
    if (!isdigit((unsigned char)*cur->cur)) {
        return -1;
    }
    while (isdigit((unsigned char)*cur->cur)) {
        cur->cur++;
    }
    if (*cur->cur == '.') {
        cur->cur++;
        if (!isdigit((unsigned char)*cur->cur)) {
            return -1;
        }
        while (isdigit((unsigned char)*cur->cur)) {
            cur->cur++;
        }
    }
    if (*cur->cur == 'e' || *cur->cur == 'E') {
        cur->cur++;
        if (*cur->cur == '+' || *cur->cur == '-') {
            cur->cur++;
        }
        if (!isdigit((unsigned char)*cur->cur)) {
            return -1;
        }
        while (isdigit((unsigned char)*cur->cur)) {
            cur->cur++;
        }
    }
    char buf[64];
    size_t len = (size_t)(cur->cur - start);
    if (len >= sizeof(buf)) {
        return -1;
    }
    memcpy(buf, start, len);
    buf[len] = '\0';
    char *end = NULL;
    errno = 0;
    double value = strtod(buf, &end);
    if (errno != 0 || !end || *end != '\0') {
        return -1;
    }
    *out = value;
    return 0;
}

static int skip_literal(json_cursor_t *cur, const char *literal) {
    size_t len = strlen(literal);
    if (strncmp(cur->cur, literal, len) != 0) {
        return -1;
    }
    cur->cur += len;
    return 0;
}

static int skip_value(json_cursor_t *cur);

static int skip_array(json_cursor_t *cur) {
    if (consume_char(cur, '[') != 0) {
        return -1;
    }
    skip_ws(cur);
    if (*cur->cur == ']') {
        cur->cur++;
        return 0;
    }
    while (*cur->cur) {
        if (skip_value(cur) != 0) {
            return -1;
        }
        skip_ws(cur);
        if (*cur->cur == ',') {
            cur->cur++;
            continue;
        }
        if (*cur->cur == ']') {
            cur->cur++;
            return 0;
        }
        break;
    }
    return -1;
}

static int skip_object(json_cursor_t *cur) {
    if (consume_char(cur, '{') != 0) {
        return -1;
    }
    skip_ws(cur);
    if (*cur->cur == '}') {
        cur->cur++;
        return 0;
    }
    while (*cur->cur) {
        if (parse_string(cur, NULL, 0) != 0) {
            return -1;
        }
        if (consume_char(cur, ':') != 0) {
            return -1;
        }
        if (skip_value(cur) != 0) {
            return -1;
        }
        skip_ws(cur);
        if (*cur->cur == ',') {
            cur->cur++;
            continue;
        }
        if (*cur->cur == '}') {
            cur->cur++;
            return 0;
        }
        break;
    }
    return -1;
}

static int skip_value(json_cursor_t *cur) {
    skip_ws(cur);
    if (*cur->cur == '"') {
        return parse_string(cur, NULL, 0);
    }
    if (*cur->cur == '{') {
        return skip_object(cur);
    }
    if (*cur->cur == '[') {
        return skip_array(cur);
    }
    if (*cur->cur == '-' || isdigit((unsigned char)*cur->cur)) {
        double dummy = 0.0;
        return parse_double(cur, &dummy);
    }
    if (skip_literal(cur, "true") == 0 || skip_literal(cur, "false") == 0 ||
        skip_literal(cur, "null") == 0) {
        return 0;
    }
    return -1;
}

static int parse_http_object(json_cursor_t *cur, kolibri_config_t *cfg) {
    if (consume_char(cur, '{') != 0) {
        return -1;
    }
    int saw_host = 0;
    int saw_port = 0;
    int saw_body_size = 0;
    while (*cur->cur) {
        skip_ws(cur);
        if (*cur->cur == '}') {
            cur->cur++;
            break;
        }
        char key[32];
        if (parse_string(cur, key, sizeof(key)) != 0) {
            return -1;
        }
        if (consume_char(cur, ':') != 0) {
            return -1;
        }
        if (strcmp(key, "host") == 0) {
            if (saw_host) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                if (parse_string(cur, cfg->http.host, sizeof(cfg->http.host)) != 0) {
                    return -1;
                }
                saw_host = 1;
            }
        } else if (strcmp(key, "port") == 0) {
            if (saw_port) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT16_MAX) {
                    return -1;
                }
                cfg->http.port = (uint16_t)value;
                saw_port = 1;
            }
        } else if (strcmp(key, "max_body_size") == 0) {
            if (saw_body_size) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->http.max_body_size = (uint32_t)value;
                saw_body_size = 1;
            }
        } else {
            if (skip_value(cur) != 0) {
                return -1;
            }
        }
        skip_ws(cur);
        if (*cur->cur == ',') {
            cur->cur++;
            continue;
        }
        if (*cur->cur == '}') {
            cur->cur++;
            break;
        }
        return -1;
    }
    if (!saw_host || !saw_port) {
        return -1;
    }
    return 0;
}

static int parse_vm_object(json_cursor_t *cur, kolibri_config_t *cfg) {
    if (consume_char(cur, '{') != 0) {
        return -1;
    }
    int saw_steps = 0;
    int saw_stack = 0;
    int saw_trace = 0;
    while (*cur->cur) {
        skip_ws(cur);
        if (*cur->cur == '}') {
            cur->cur++;
            break;
        }
        char key[32];
        if (parse_string(cur, key, sizeof(key)) != 0) {
            return -1;
        }
        if (consume_char(cur, ':') != 0) {
            return -1;
        }
        if (strcmp(key, "max_steps") == 0) {
            if (saw_steps) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->vm.max_steps = (uint32_t)value;
                saw_steps = 1;
            }
        } else if (strcmp(key, "max_stack") == 0) {
            if (saw_stack) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->vm.max_stack = (uint32_t)value;
                saw_stack = 1;
            }
        } else if (strcmp(key, "trace_depth") == 0) {
            if (saw_trace) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->vm.trace_depth = (uint32_t)value;
                saw_trace = 1;
            }
        } else {
            return -1;
        }
        skip_ws(cur);
        if (*cur->cur == ',') {
            cur->cur++;
            continue;
        }
        if (*cur->cur == '}') {
            cur->cur++;
            break;
        }
        return -1;
    }
    if (!saw_steps || !saw_stack || !saw_trace) {
        return -1;
    }
    return 0;
}

static int parse_fkv_object(json_cursor_t *cur, kolibri_config_t *cfg) {
    if (consume_char(cur, '{') != 0) {
        return -1;
    }
    int saw_top_k = 0;
    while (*cur->cur) {
        skip_ws(cur);
        if (*cur->cur == '}') {
            cur->cur++;
            return 0;
        }
        char key[32];
        if (parse_string(cur, key, sizeof(key)) != 0) {
            return -1;
        }
        if (consume_char(cur, ':') != 0) {
            return -1;
        }
        if (strcmp(key, "top_k") == 0) {
            if (saw_top_k) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->fkv.top_k = value == 0 ? 1u : (uint32_t)value;
                saw_top_k = 1;
            }
        } else {
            if (skip_value(cur) != 0) {
                return -1;
            }
        }
        skip_ws(cur);
        if (*cur->cur == ',') {
            cur->cur++;
            continue;
        }
        if (*cur->cur == '}') {
            cur->cur++;
            return 0;
        }
        return -1;
    }
    return -1;
}

static int parse_ai_object(json_cursor_t *cur, kolibri_config_t *cfg) {
    if (consume_char(cur, '{') != 0) {
        return -1;
    }
    int saw_path = 0;
    int saw_limit = 0;
    while (*cur->cur) {
        skip_ws(cur);
        if (*cur->cur == '}') {
            cur->cur++;
            return 0;
        }
        char key[32];
        if (parse_string(cur, key, sizeof(key)) != 0) {
            return -1;
        }
        if (consume_char(cur, ':') != 0) {
            return -1;
        }
        if (strcmp(key, "snapshot_path") == 0) {
            if (saw_path) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                if (parse_string(cur, cfg->ai.snapshot_path, sizeof(cfg->ai.snapshot_path)) != 0) {
                    return -1;
                }
                saw_path = 1;
            }
        } else if (strcmp(key, "snapshot_limit") == 0) {
            if (saw_limit) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->ai.snapshot_limit = (uint32_t)value;
                saw_limit = 1;
            }
        } else {
            if (skip_value(cur) != 0) {
                return -1;
            }
        }
        skip_ws(cur);
        if (*cur->cur == ',') {
            cur->cur++;
            continue;
        }
        if (*cur->cur == '}') {
            cur->cur++;
            return 0;
        }
        return -1;
    }
    return -1;
}

static int parse_selfplay_object(json_cursor_t *cur, kolibri_config_t *cfg) {
    if (consume_char(cur, '{') != 0) {
        return -1;
    }
    int saw_tasks = 0;
    int saw_difficulty = 0;
    while (*cur->cur) {
        skip_ws(cur);
        if (*cur->cur == '}') {
            cur->cur++;
            return 0;
        }
        char key[32];
        if (parse_string(cur, key, sizeof(key)) != 0) {
            return -1;
        }
        if (consume_char(cur, ':') != 0) {
            return -1;
        }
        if (strcmp(key, "tasks_per_iteration") == 0) {
            if (saw_tasks) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->selfplay.tasks_per_iteration = (uint32_t)value;
                saw_tasks = 1;
            }
        } else if (strcmp(key, "max_difficulty") == 0) {
            if (saw_difficulty) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->selfplay.max_difficulty = (uint32_t)value;
                saw_difficulty = 1;
            }
        } else {
            return -1;
        }
        skip_ws(cur);
        if (*cur->cur == ',') {
            cur->cur++;
            continue;
        }
        if (*cur->cur == '}') {
            cur->cur++;
            return 0;
        }
        return -1;
    }
    return -1;
}

static int parse_search_object(json_cursor_t *cur, kolibri_config_t *cfg) {
    if (consume_char(cur, '{') != 0) {
        return -1;
    }
    int saw_candidates = 0;
    int saw_terms = 0;
    int saw_coefficient = 0;
    int saw_formula_length = 0;
    int saw_base_effectiveness = 0;
    while (*cur->cur) {
        skip_ws(cur);
        if (*cur->cur == '}') {
            cur->cur++;
            return 0;
        }
        char key[32];
        if (parse_string(cur, key, sizeof(key)) != 0) {
            return -1;
        }
        if (consume_char(cur, ':') != 0) {
            return -1;
        }
        if (strcmp(key, "base_effectiveness") == 0) {
            if (saw_base_effectiveness) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                double value = 0.0;
                if (parse_double(cur, &value) != 0) {
                    return -1;
                }
                if (value >= 0.0) {
                    cfg->search.base_effectiveness = value;
                }
                saw_base_effectiveness = 1;
            }
        } else if (strcmp(key, "max_candidates") == 0) {
            if (saw_candidates) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->search.max_candidates = (uint32_t)value;
                saw_candidates = 1;
            }
        } else if (strcmp(key, "max_terms") == 0) {
            if (saw_terms) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->search.max_terms = (uint32_t)value;
                saw_terms = 1;
            }
        } else if (strcmp(key, "max_coefficient") == 0) {
            if (saw_coefficient) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->search.max_coefficient = (uint32_t)value;
                saw_coefficient = 1;
            }
        } else if (strcmp(key, "max_formula_length") == 0) {
            if (saw_formula_length) {
                if (skip_value(cur) != 0) {
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(cur, &value) != 0 || value > UINT32_MAX) {
                    return -1;
                }
                cfg->search.max_formula_length = (uint32_t)value;
                saw_formula_length = 1;
            }
        } else {
            return -1;
        }
        skip_ws(cur);
        if (*cur->cur == ',') {
            cur->cur++;
            continue;
        }
        if (*cur->cur == '}') {
            cur->cur++;
            return 0;
        }
        return -1;
    }
    return -1;
}

static void strip_comments(char *buffer) {
    char *src = buffer;
    char *dst = buffer;
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
        if (*src == '"' && (src == buffer || src[-1] != '\\')) {
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

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    FILE *fp = fopen(path, "rb");
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

    char *buffer = calloc(1, (size_t)len + 1);
    if (!buffer) {
        fclose(fp);
        return -1;
    }

    size_t read = fread(buffer, 1, (size_t)len, fp);
    fclose(fp);
    buffer[read] = '\0';

    strip_comments(buffer);

    json_cursor_t cur = {buffer};
    kolibri_config_t tmp = *cfg;

    if (consume_char(&cur, '{') != 0) {
        free(buffer);
        errno = EINVAL;
        return -1;
    }

    int saw_http = 0;
    int saw_vm = 0;
    int saw_seed = 0;
    int saw_fkv = 0;
    int saw_ai = 0;
    int saw_selfplay = 0;
    int saw_search = 0;

    while (*cur.cur) {
        skip_ws(&cur);
        if (*cur.cur == '}') {
            cur.cur++;
            break;
        }

        char key[32];
        if (parse_string(&cur, key, sizeof(key)) != 0) {
            free(buffer);
            errno = EINVAL;
            return -1;
        }

        if (consume_char(&cur, ':') != 0) {
            free(buffer);
            errno = EINVAL;
            return -1;
        }

        if (strcmp(key, "http") == 0) {
            if (saw_http) {
                if (skip_value(&cur) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
            } else {
                if (parse_http_object(&cur, &tmp) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
                saw_http = 1;
            }
        } else if (strcmp(key, "vm") == 0) {
            if (saw_vm) {
                if (skip_value(&cur) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
            } else {
                if (parse_vm_object(&cur, &tmp) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
                saw_vm = 1;
            }
        } else if (strcmp(key, "seed") == 0) {
            if (saw_seed) {
                if (skip_value(&cur) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
            } else {
                uint64_t value = 0;
                if (parse_uint(&cur, &value) != 0 || value > UINT32_MAX) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
                tmp.seed = (uint32_t)value;
                saw_seed = 1;
            }
        } else if (strcmp(key, "fkv") == 0) {
            if (saw_fkv) {
                if (skip_value(&cur) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
            } else {
                if (parse_fkv_object(&cur, &tmp) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
                saw_fkv = 1;
            }
        } else if (strcmp(key, "ai") == 0) {
            if (saw_ai) {
                if (skip_value(&cur) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
            } else {
                if (parse_ai_object(&cur, &tmp) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
                saw_ai = 1;
            }
        } else if (strcmp(key, "selfplay") == 0) {
            if (saw_selfplay) {
                if (skip_value(&cur) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
            } else {
                if (parse_selfplay_object(&cur, &tmp) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
                saw_selfplay = 1;
            }
        } else if (strcmp(key, "search") == 0) {
            if (saw_search) {
                if (skip_value(&cur) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
            } else {
                if (parse_search_object(&cur, &tmp) != 0) {
                    free(buffer);
                    errno = EINVAL;
                    return -1;
                }
                saw_search = 1;
            }
        } else {
            if (skip_value(&cur) != 0) {
                free(buffer);
                errno = EINVAL;
                return -1;
            }
        }

        skip_ws(&cur);
        if (*cur.cur == ',') {
            cur.cur++;
            continue;
        }
        if (*cur.cur == '}') {
            cur.cur++;
            break;
        }
        free(buffer);
        errno = EINVAL;
        return -1;
    }

    free(buffer);

    if (!saw_http || !saw_vm || !saw_seed) {
        errno = EINVAL;
        return -1;
    }

    *cfg = tmp;
    return 0;
}
