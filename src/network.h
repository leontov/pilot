#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <stdbool.h>

// Инициализация сетевого подключения
bool network_init(int port);

// Отправка данных другому узлу
bool network_send_data(const char* host, int port, const char* data);

// Получение данных от других узлов
char* network_receive_data(void);

// Освобождение ресурсов
void network_cleanup(void);

#endif // NETWORK_H
