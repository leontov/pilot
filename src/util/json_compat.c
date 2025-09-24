#include "json-c/json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *json_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, s, len + 1);
    return copy;
}

struct json_pair {
    char *key;
    json_object *value;
};

struct json_array_data {
    json_object **items;
    size_t count;
    size_t capacity;
};

struct json_object {
    enum json_type type;
    union {
        double dbl;
        long long integer;
        char *string;
        struct {
            struct json_pair *pairs;
            size_t count;
            size_t capacity;
        } object;
        struct json_array_data array;
    } data;
    char *cached_string;
};

struct json_tokener {
    const char *input;
    size_t length;
    size_t pos;
    enum json_tokener_error err;
};

static json_object *json_object_alloc(enum json_type type) {
    json_object *obj = calloc(1, sizeof(*obj));
    if (!obj) {
        return NULL;
    }
    obj->type = type;
    return obj;
}

json_object *json_object_new_object(void) {
    return json_object_alloc(json_type_object);
}

json_object *json_object_new_array(void) {
    return json_object_alloc(json_type_array);
}

json_object *json_object_new_string(const char *s) {
    json_object *obj = json_object_alloc(json_type_string);
    if (!obj) {
        return NULL;
    }
    if (s) {
        obj->data.string = json_strdup(s);
        if (!obj->data.string) {
            free(obj);
            return NULL;
        }
    }
    return obj;
}

json_object *json_object_new_double(double val) {
    json_object *obj = json_object_alloc(json_type_double);
    if (obj) {
        obj->data.dbl = val;
    }
    return obj;
}

json_object *json_object_new_int64(long long val) {
    json_object *obj = json_object_alloc(json_type_int);
    if (obj) {
        obj->data.integer = val;
    }
    return obj;
}

static int ensure_pair_capacity(json_object *obj, size_t needed) {
    if (obj->data.object.capacity >= needed) {
        return 0;
    }
    size_t new_capacity = obj->data.object.capacity ? obj->data.object.capacity * 2 : 4;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    struct json_pair *pairs = realloc(obj->data.object.pairs, new_capacity * sizeof(*pairs));
    if (!pairs) {
        return -1;
    }
    obj->data.object.pairs = pairs;
    obj->data.object.capacity = new_capacity;
    return 0;
}

void json_object_object_add(json_object *obj, const char *key, json_object *val) {
    if (!obj || obj->type != json_type_object || !key || !val) {
        return;
    }
    if (ensure_pair_capacity(obj, obj->data.object.count + 1) != 0) {
        return;
    }
    struct json_pair *pair = &obj->data.object.pairs[obj->data.object.count];
    pair->key = json_strdup(key);
    if (!pair->key) {
        json_object_put(val);
        return;
    }
    pair->value = val;
    obj->data.object.count++;
}

static int ensure_array_capacity(struct json_array_data *arr, size_t needed) {
    if (arr->capacity >= needed) {
        return 0;
    }
    size_t new_capacity = arr->capacity ? arr->capacity * 2 : 4;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    json_object **items = realloc(arr->items, new_capacity * sizeof(*items));
    if (!items) {
        return -1;
    }
    arr->items = items;
    arr->capacity = new_capacity;
    return 0;
}

void json_object_array_add(json_object *array, json_object *val) {
    if (!array || array->type != json_type_array || !val) {
        return;
    }
    struct json_array_data *arr = &array->data.array;
    if (ensure_array_capacity(arr, arr->count + 1) != 0) {
        json_object_put(val);
        return;
    }
    arr->items[arr->count++] = val;
}

static void buffer_append(char **buf, size_t *len, size_t *cap, const char *data, size_t n) {
    if (*len + n + 1 > *cap) {
        size_t new_cap = *cap ? *cap * 2 : 64;
        while (new_cap < *len + n + 1) {
            new_cap *= 2;
        }
        char *tmp = realloc(*buf, new_cap);
        if (!tmp) {
            return;
        }
        *buf = tmp;
        *cap = new_cap;
    }
    memcpy(*buf + *len, data, n);
    *len += n;
    (*buf)[*len] = '\0';
}

static void append_escaped_string(char **buf, size_t *len, size_t *cap, const char *str) {
    buffer_append(buf, len, cap, "\"", 1);
    while (str && *str) {
        unsigned char ch = (unsigned char)*str++;
        switch (ch) {
        case '\"':
            buffer_append(buf, len, cap, "\\\"", 2);
            break;
        case '\\':
            buffer_append(buf, len, cap, "\\\\", 2);
            break;
        case '\n':
            buffer_append(buf, len, cap, "\\n", 2);
            break;
        case '\r':
            buffer_append(buf, len, cap, "\\r", 2);
            break;
        case '\t':
            buffer_append(buf, len, cap, "\\t", 2);
            break;
        default:
            if (ch < 0x20) {
                char tmp[7];
                snprintf(tmp, sizeof(tmp), "\\u%04x", ch);
                buffer_append(buf, len, cap, tmp, strlen(tmp));
            } else {
                char c = (char)ch;
                buffer_append(buf, len, cap, &c, 1);
            }
            break;
        }
    }
    buffer_append(buf, len, cap, "\"", 1);
}

static void json_object_serialize(json_object *obj, char **buf, size_t *len, size_t *cap);

static void serialize_object(json_object *obj, char **buf, size_t *len, size_t *cap) {
    buffer_append(buf, len, cap, "{", 1);
    for (size_t i = 0; i < obj->data.object.count; ++i) {
        if (i > 0) {
            buffer_append(buf, len, cap, ",", 1);
        }
        append_escaped_string(buf, len, cap, obj->data.object.pairs[i].key);
        buffer_append(buf, len, cap, ":", 1);
        json_object_serialize(obj->data.object.pairs[i].value, buf, len, cap);
    }
    buffer_append(buf, len, cap, "}", 1);
}

static void serialize_array(json_object *obj, char **buf, size_t *len, size_t *cap) {
    buffer_append(buf, len, cap, "[", 1);
    for (size_t i = 0; i < obj->data.array.count; ++i) {
        if (i > 0) {
            buffer_append(buf, len, cap, ",", 1);
        }
        json_object_serialize(obj->data.array.items[i], buf, len, cap);
    }
    buffer_append(buf, len, cap, "]", 1);
}

static void json_object_serialize(json_object *obj, char **buf, size_t *len, size_t *cap) {
    switch (obj->type) {
    case json_type_null:
        buffer_append(buf, len, cap, "null", 4);
        break;
    case json_type_boolean:
        buffer_append(buf, len, cap, obj->data.integer ? "true" : "false", obj->data.integer ? 4 : 5);
        break;
    case json_type_double: {
        char tmp[64];
        int written = snprintf(tmp, sizeof(tmp), "%g", obj->data.dbl);
        if (written < 0) {
            written = 0;
        }
        buffer_append(buf, len, cap, tmp, (size_t)written);
        break;
    }
    case json_type_int: {
        char tmp[64];
        int written = snprintf(tmp, sizeof(tmp), "%lld", obj->data.integer);
        if (written < 0) {
            written = 0;
        }
        buffer_append(buf, len, cap, tmp, (size_t)written);
        break;
    }
    case json_type_string:
        append_escaped_string(buf, len, cap, obj->data.string ? obj->data.string : "");
        break;
    case json_type_object:
        serialize_object(obj, buf, len, cap);
        break;
    case json_type_array:
        serialize_array(obj, buf, len, cap);
        break;
    }
}

const char *json_object_to_json_string_ext(json_object *obj, int flags) {
    (void)flags;
    if (!obj) {
        return "null";
    }
    free(obj->cached_string);
    obj->cached_string = NULL;
    size_t cap = 0;
    size_t len = 0;
    char *buf = NULL;
    json_object_serialize(obj, &buf, &len, &cap);
    if (!buf) {
        return NULL;
    }
    obj->cached_string = buf;
    return obj->cached_string;
}

static void json_object_free(json_object *obj) {
    if (!obj) {
        return;
    }
    switch (obj->type) {
    case json_type_string:
        free(obj->data.string);
        break;
    case json_type_object:
        for (size_t i = 0; i < obj->data.object.count; ++i) {
            free(obj->data.object.pairs[i].key);
            json_object_put(obj->data.object.pairs[i].value);
        }
        free(obj->data.object.pairs);
        break;
    case json_type_array:
        for (size_t i = 0; i < obj->data.array.count; ++i) {
            json_object_put(obj->data.array.items[i]);
        }
        free(obj->data.array.items);
        break;
    default:
        break;
    }
    free(obj->cached_string);
    free(obj);
}

void json_object_put(json_object *obj) {
    json_object_free(obj);
}

int json_object_object_get_ex(const json_object *obj, const char *key, json_object **out) {
    if (!obj || obj->type != json_type_object || !key) {
        return 0;
    }
    for (size_t i = 0; i < obj->data.object.count; ++i) {
        if (strcmp(obj->data.object.pairs[i].key, key) == 0) {
            if (out) {
                *out = obj->data.object.pairs[i].value;
            }
            return 1;
        }
    }
    return 0;
}

size_t json_object_array_length(const json_object *obj) {
    if (!obj || obj->type != json_type_array) {
        return 0;
    }
    return obj->data.array.count;
}

json_object *json_object_array_get_idx(const json_object *obj, int idx) {
    if (!obj || obj->type != json_type_array || idx < 0 || (size_t)idx >= obj->data.array.count) {
        return NULL;
    }
    return obj->data.array.items[idx];
}

int json_object_is_type(const json_object *obj, enum json_type type) {
    return obj && obj->type == type;
}

double json_object_get_double(const json_object *obj) {
    if (!obj) {
        return 0.0;
    }
    if (obj->type == json_type_double) {
        return obj->data.dbl;
    }
    if (obj->type == json_type_int || obj->type == json_type_boolean) {
        return (double)obj->data.integer;
    }
    if (obj->type == json_type_string && obj->data.string) {
        return strtod(obj->data.string, NULL);
    }
    return 0.0;
}

long long json_object_get_int64(const json_object *obj) {
    if (!obj) {
        return 0;
    }
    if (obj->type == json_type_int || obj->type == json_type_boolean) {
        return obj->data.integer;
    }
    if (obj->type == json_type_double) {
        return (long long)obj->data.dbl;
    }
    if (obj->type == json_type_string && obj->data.string) {
        return strtoll(obj->data.string, NULL, 10);
    }
    return 0;
}

const char *json_object_get_string(const json_object *obj) {
    if (!obj) {
        return NULL;
    }
    if (obj->type == json_type_string) {
        return obj->data.string ? obj->data.string : "";
    }
    return json_object_to_json_string_ext((json_object *)obj, JSON_C_TO_STRING_PLAIN);
}

json_tokener *json_tokener_new(void) {
    json_tokener *tok = calloc(1, sizeof(*tok));
    if (tok) {
        tok->err = json_tokener_success;
    }
    return tok;
}

static void skip_ws(json_tokener *tok) {
    while (tok->pos < tok->length && isspace((unsigned char)tok->input[tok->pos])) {
        tok->pos++;
    }
}

static json_object *parse_value(json_tokener *tok);

static char peek(json_tokener *tok) {
    return tok->pos < tok->length ? tok->input[tok->pos] : '\0';
}

static char advance(json_tokener *tok) {
    if (tok->pos < tok->length) {
        return tok->input[tok->pos++];
    }
    return '\0';
}

static char *parse_string_literal(json_tokener *tok) {
    if (advance(tok) != '"') {
        tok->err = json_tokener_error_parse;
        return NULL;
    }
    size_t cap = 16;
    size_t len = 0;
    char *out = malloc(cap);
    if (!out) {
        tok->err = json_tokener_error_parse;
        return NULL;
    }
    while (tok->pos < tok->length) {
        char ch = advance(tok);
        if (ch == '"') {
            out[len] = '\0';
            return out;
        }
        if (ch == '\\') {
            if (tok->pos >= tok->length) {
                break;
            }
            char esc = advance(tok);
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
                tok->err = json_tokener_error_parse;
                free(out);
                return NULL;
            }
        }
        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            char *tmp = realloc(out, new_cap);
            if (!tmp) {
                free(out);
                tok->err = json_tokener_error_parse;
                return NULL;
            }
            out = tmp;
            cap = new_cap;
        }
        out[len++] = ch;
    }
    free(out);
    tok->err = json_tokener_error_parse;
    return NULL;
}

static json_object *parse_number(json_tokener *tok) {
    size_t start = tok->pos;
    if (peek(tok) == '-') {
        tok->pos++;
    }
    while (isdigit((unsigned char)peek(tok))) {
        tok->pos++;
    }
    int is_double = 0;
    if (peek(tok) == '.') {
        is_double = 1;
        tok->pos++;
        while (isdigit((unsigned char)peek(tok))) {
            tok->pos++;
        }
    }
    if (peek(tok) == 'e' || peek(tok) == 'E') {
        is_double = 1;
        tok->pos++;
        if (peek(tok) == '+' || peek(tok) == '-') {
            tok->pos++;
        }
        while (isdigit((unsigned char)peek(tok))) {
            tok->pos++;
        }
    }
    size_t end = tok->pos;
    size_t len = end - start;
    char *buf = malloc(len + 1);
    if (!buf) {
        tok->err = json_tokener_error_parse;
        return NULL;
    }
    memcpy(buf, tok->input + start, len);
    buf[len] = '\0';
    json_object *obj = NULL;
    if (is_double) {
        double val = strtod(buf, NULL);
        obj = json_object_new_double(val);
    } else {
        long long val = strtoll(buf, NULL, 10);
        obj = json_object_new_int64(val);
    }
    free(buf);
    if (!obj) {
        tok->err = json_tokener_error_parse;
    }
    return obj;
}

static json_object *parse_array(json_tokener *tok) {
    if (advance(tok) != '[') {
        tok->err = json_tokener_error_parse;
        return NULL;
    }
    json_object *arr = json_object_new_array();
    if (!arr) {
        tok->err = json_tokener_error_parse;
        return NULL;
    }
    skip_ws(tok);
    if (peek(tok) == ']') {
        advance(tok);
        return arr;
    }
    while (tok->pos < tok->length) {
        json_object *value = parse_value(tok);
        if (!value) {
            json_object_put(arr);
            return NULL;
        }
        json_object_array_add(arr, value);
        skip_ws(tok);
        char ch = peek(tok);
        if (ch == ',') {
            advance(tok);
            skip_ws(tok);
            continue;
        }
        if (ch == ']') {
            advance(tok);
            return arr;
        }
        break;
    }
    json_object_put(arr);
    tok->err = json_tokener_error_parse;
    return NULL;
}

static json_object *parse_object(json_tokener *tok) {
    if (advance(tok) != '{') {
        tok->err = json_tokener_error_parse;
        return NULL;
    }
    json_object *obj = json_object_new_object();
    if (!obj) {
        tok->err = json_tokener_error_parse;
        return NULL;
    }
    skip_ws(tok);
    if (peek(tok) == '}') {
        advance(tok);
        return obj;
    }
    while (tok->pos < tok->length) {
        skip_ws(tok);
        char *key = parse_string_literal(tok);
        if (!key) {
            json_object_put(obj);
            return NULL;
        }
        skip_ws(tok);
        if (advance(tok) != ':') {
            free(key);
            json_object_put(obj);
            tok->err = json_tokener_error_parse;
            return NULL;
        }
        skip_ws(tok);
        json_object *value = parse_value(tok);
        if (!value) {
            free(key);
            json_object_put(obj);
            return NULL;
        }
        json_object_object_add(obj, key, value);
        free(key);
        skip_ws(tok);
        char ch = peek(tok);
        if (ch == ',') {
            advance(tok);
            skip_ws(tok);
            continue;
        }
        if (ch == '}') {
            advance(tok);
            return obj;
        }
        break;
    }
    json_object_put(obj);
    tok->err = json_tokener_error_parse;
    return NULL;
}

static json_object *parse_value(json_tokener *tok) {
    skip_ws(tok);
    char ch = peek(tok);
    if (ch == '{') {
        return parse_object(tok);
    }
    if (ch == '[') {
        return parse_array(tok);
    }
    if (ch == '"') {
        char *str = parse_string_literal(tok);
        if (!str) {
            return NULL;
        }
        json_object *obj = json_object_new_string(str);
        free(str);
        if (!obj) {
            tok->err = json_tokener_error_parse;
        }
        return obj;
    }
    if (ch == '-' || isdigit((unsigned char)ch)) {
        return parse_number(tok);
    }
    if (strncmp(tok->input + tok->pos, "null", 4) == 0) {
        tok->pos += 4;
        return json_object_alloc(json_type_null);
    }
    if (strncmp(tok->input + tok->pos, "true", 4) == 0) {
        tok->pos += 4;
        json_object *obj = json_object_new_int64(1);
        if (obj) {
            obj->type = json_type_boolean;
        }
        return obj;
    }
    if (strncmp(tok->input + tok->pos, "false", 5) == 0) {
        tok->pos += 5;
        json_object *obj = json_object_new_int64(0);
        if (obj) {
            obj->type = json_type_boolean;
        }
        return obj;
    }
    tok->err = json_tokener_error_parse;
    return NULL;
}

json_object *json_tokener_parse_ex(json_tokener *tok, const char *str, int len) {
    if (!tok || !str) {
        if (tok) {
            tok->err = json_tokener_error_parse;
        }
        return NULL;
    }
    tok->input = str;
    tok->length = len >= 0 ? (size_t)len : strlen(str);
    tok->pos = 0;
    tok->err = json_tokener_success;
    json_object *obj = parse_value(tok);
    if (!obj) {
        tok->err = json_tokener_error_parse;
        return NULL;
    }
    skip_ws(tok);
    if (tok->pos != tok->length) {
        json_object_put(obj);
        tok->err = json_tokener_error_parse;
        return NULL;
    }
    return obj;
}

enum json_tokener_error json_tokener_get_error(json_tokener *tok) {
    return tok ? tok->err : json_tokener_error_parse;
}

void json_tokener_free(json_tokener *tok) {
    free(tok);
}
