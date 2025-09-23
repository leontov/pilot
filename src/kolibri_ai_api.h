#ifndef KOLIBRI_AI_API_H
#define KOLIBRI_AI_API_H

#include "kolibri_ai.h"
#include <microhttpd.h>

// Инициализация API сервера
int ai_api_init(KolibriAI* ai, uint16_t port);

// Остановка API сервера
void ai_api_stop(void);

#endif // KOLIBRI_AI_API_H
