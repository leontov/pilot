#ifndef JSON_C_JSON_H
#define JSON_C_JSON_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum json_type {
    json_type_null = 0,
    json_type_boolean = 1,
    json_type_double = 2,
    json_type_int = 3,
    json_type_object = 4,
    json_type_array = 5,
    json_type_string = 6
};

enum json_tokener_error {
    json_tokener_success = 0,
    json_tokener_error_parse = 1
};

typedef struct json_object json_object;
typedef struct json_tokener json_tokener;

#define JSON_C_TO_STRING_PLAIN 0

json_object *json_object_new_object(void);
json_object *json_object_new_array(void);
json_object *json_object_new_string(const char *s);
json_object *json_object_new_double(double val);
json_object *json_object_new_int64(long long val);
void json_object_object_add(json_object *obj, const char *key, json_object *val);
void json_object_array_add(json_object *array, json_object *val);
const char *json_object_to_json_string_ext(json_object *obj, int flags);
void json_object_put(json_object *obj);
int json_object_object_get_ex(const json_object *obj, const char *key, json_object **out);
size_t json_object_array_length(const json_object *obj);
json_object *json_object_array_get_idx(const json_object *obj, int idx);
int json_object_is_type(const json_object *obj, enum json_type type);
double json_object_get_double(const json_object *obj);
long long json_object_get_int64(const json_object *obj);
const char *json_object_get_string(const json_object *obj);

json_tokener *json_tokener_new(void);
json_object *json_tokener_parse_ex(json_tokener *tok, const char *str, int len);
enum json_tokener_error json_tokener_get_error(json_tokener *tok);
void json_tokener_free(json_tokener *tok);

#ifdef __cplusplus
}
#endif

#endif /* JSON_C_JSON_H */
