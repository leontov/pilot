/* Copyright (c) 2024 Кочуров Владислав Евгеньевич */

#include "util/log.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static log_level_t current_level = LOG_LEVEL_INFO;
static FILE *log_fp = NULL;

static const char *level_name(log_level_t level) {
    switch (level) {
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_ERROR:
    default:
        return "ERROR";
    }
}

void log_set_level(log_level_t level) {
    current_level = level;
}

void log_set_file(FILE *fp) {
    log_fp = fp;
}

static void log_emit(log_level_t level, const char *fmt, va_list ap) {
    if (level < current_level) {
        return;
    }
    FILE *out = log_fp ? log_fp : stderr;
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_now);

    fprintf(out, "%s [%s] ", buf, level_name(level));
    vfprintf(out, fmt, ap);
    fputc('\n', out);
    fflush(out);
}

void log_debug(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_emit(LOG_LEVEL_DEBUG, fmt, ap);
    va_end(ap);
}

void log_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_emit(LOG_LEVEL_INFO, fmt, ap);
    va_end(ap);
}

void log_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_emit(LOG_LEVEL_WARN, fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_emit(LOG_LEVEL_ERROR, fmt, ap);
    va_end(ap);
}
