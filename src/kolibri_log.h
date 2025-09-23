#ifndef KOLIBRI_LOG_H
#define KOLIBRI_LOG_H

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define LOG_AI(fmt, ...) \
    printf(ANSI_COLOR_CYAN "[AI] " fmt ANSI_COLOR_RESET "\n", ##__VA_ARGS__)

#define LOG_SUCCESS(fmt, ...) \
    printf(ANSI_COLOR_GREEN "[SUCCESS] " fmt ANSI_COLOR_RESET "\n", ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    printf(ANSI_COLOR_RED "[ERROR] " fmt ANSI_COLOR_RESET "\n", ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    printf(ANSI_COLOR_BLUE "[INFO] " fmt ANSI_COLOR_RESET "\n", ##__VA_ARGS__)

#define LOG_WARNING(fmt, ...) \
    printf(ANSI_COLOR_YELLOW "[WARNING] " fmt ANSI_COLOR_RESET "\n", ##__VA_ARGS__)

#endif // KOLIBRI_LOG_H
