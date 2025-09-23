#define _POSIX_C_SOURCE 200809L

#include "util/config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *write_temp_file(const char *content) {
    char template[] = "/tmp/kolibri_configXXXXXX";
    int fd = mkstemp(template);
    assert(fd >= 0);
    size_t len = strlen(content);
    ssize_t written = write(fd, content, len);
    assert(written == (ssize_t)len);
    close(fd);
    return strdup(template);
}

static void remove_temp_file(char *path) {
    if (path) {
        unlink(path);
        free(path);
    }
}

static void test_config_valid(void) {
    const char *content =
        "{\n"
        "  // http configuration\n"
        "  \"http\": {\n"
        "    \"host\": \"127.0.0.1\",\n"
        "    \"port\": 8080 /* comment */\n"
        "  },\n"
        "  \"vm\": {\n"
        "    \"max_steps\": 4096,\n"
        "    \"max_stack\": 256,\n"
        "    \"trace_depth\": 32\n"
        "  },\n"
        "  \"seed\": 777\n"
        "}\n";

    char *path = write_temp_file(content);
    kolibri_config_t cfg;
    errno = 0;
    assert(config_load(path, &cfg) == 0);
    assert(strcmp(cfg.http.host, "127.0.0.1") == 0);
    assert(cfg.http.port == 8080);
    assert(cfg.vm.max_steps == 4096);
    assert(cfg.vm.max_stack == 256);
    assert(cfg.vm.trace_depth == 32);
    assert(cfg.seed == 777);
    remove_temp_file(path);
}

static void test_config_missing_field(void) {
    const char *content =
        "{\n"
        "  \"http\": {\n"
        "    \"host\": \"0.0.0.0\",\n"
        "    \"port\": 9000\n"
        "  },\n"
        "  \"seed\": 42\n"
        "}\n";

    char *path = write_temp_file(content);
    kolibri_config_t cfg;
    errno = 0;
    assert(config_load(path, &cfg) == -1);
    assert(errno == EINVAL);
    assert(strcmp(cfg.http.host, "0.0.0.0") == 0);
    assert(cfg.http.port == 9000);
    assert(cfg.vm.max_steps == 2048);
    assert(cfg.vm.max_stack == 128);
    assert(cfg.vm.trace_depth == 64);
    assert(cfg.seed == 1337);
    remove_temp_file(path);
}

static void test_config_invalid_json(void) {
    const char *content = "{ \"http\": { \"host\": \"0.0.0.0\" }"; // missing closing braces and other fields
    char *path = write_temp_file(content);
    kolibri_config_t cfg;
    errno = 0;
    assert(config_load(path, &cfg) == -1);
    assert(errno == EINVAL);
    assert(strcmp(cfg.http.host, "0.0.0.0") == 0);
    assert(cfg.http.port == 9000);
    remove_temp_file(path);
}

int main(void) {
    test_config_valid();
    test_config_missing_field();
    test_config_invalid_json();
    printf("config tests passed\n");
    return 0;
}
