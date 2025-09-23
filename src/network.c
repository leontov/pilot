#include "network.h"
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>

static CURL* curl;
static char error_buffer[CURL_ERROR_SIZE];

bool network_init(int port) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    
    if (!curl) {
        return false;
    }
    
    // Настройка базовых параметров
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_PORT, port);
    
    return true;
}

bool network_send_data(const char* host, int port, const char* data) {
    if (!curl || !host || !data) {
        return false;
    }
    
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%d/data", host, port);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    
    CURLcode res = curl_easy_perform(curl);
    return res == CURLE_OK;
}

char* network_receive_data(void) {
    // Простая заглушка для демонстрации
    return strdup("{}");
}

void network_cleanup(void) {
    if (curl) {
        curl_easy_cleanup(curl);
        curl = NULL;
    }
    curl_global_cleanup();
}
