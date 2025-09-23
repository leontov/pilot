#ifndef KOLIBRI_PROTO_H
#define KOLIBRI_PROTO_H

#include <stddef.h>
#include <stdint.h>
#include "kolibri_ping.h"

// Message types
enum {
    MSG_PING = 9,   // Проверка доступности узла
    MSG_PONG = 10,  // Ответ на проверку доступности
    MSG_RULE = 11,  // Передача правила
    MSG_SYNC = 12,  // Запрос синхронизации
    MSG_DATA = 13   // Передача данных
};

// Functions for message verification and creation
int verify_message(const char* message, size_t message_len, const char* hmac, const char* key);
int create_message(const char* payload, size_t payload_len, const char* key,
                  char* buffer, size_t buffer_size);

#endif
