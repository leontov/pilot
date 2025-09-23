#include "kolibri_ai_api.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <json-c/json.h>

static struct MHD_Daemon* http_daemon = NULL;
static KolibriAI* global_ai = NULL;

// Обработчик HTTP запросов
static enum MHD_Result handle_request(void* cls,
                                    struct MHD_Connection* connection,
                                    const char* url,
                                    const char* method,
                                    const char* version,
                                    const char* upload_data,
                                    size_t* upload_data_size,
                                    void** con_cls) {
    (void)cls;
    (void)version;
    (void)con_cls;
    
    struct MHD_Response* response;
    enum MHD_Result ret;
    
    // GET /ai/status - получить статус AI
    if (strcmp(method, "GET") == 0 && strcmp(url, "/ai/status") == 0) {
        char* state = kolibri_ai_serialize_state(global_ai);
        if (!state) {
            response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
            ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
        } else {
            response = MHD_create_response_from_buffer(strlen(state),
                                                     (void*)state,
                                                     MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(response, "Content-Type", "application/json");
            ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        }
        MHD_destroy_response(response);
        return ret;
    }
    
    // GET /ai/best - получить лучшую формулу
    if (strcmp(method, "GET") == 0 && strcmp(url, "/ai/best") == 0) {
        Formula* best = kolibri_ai_get_best_formula(global_ai);
        if (!best) {
            response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
            ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        } else {
            char* json = serialize_formula(best);
            free(best);
            
            if (!json) {
                response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
                ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
            } else {
                response = MHD_create_response_from_buffer(strlen(json),
                                                         (void*)json,
                                                         MHD_RESPMEM_MUST_FREE);
                MHD_add_response_header(response, "Content-Type", "application/json");
                ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            }
        }
        MHD_destroy_response(response);
        return ret;
    }
    
    // POST /ai/formula - добавить новую формулу
    if (strcmp(method, "POST") == 0 && strcmp(url, "/ai/formula") == 0) {
        if (*upload_data_size != 0) {
            int result = kolibri_ai_process_remote_formula(global_ai, upload_data);
            *upload_data_size = 0;
            
            const char* status = result == 0 ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}";
            response = MHD_create_response_from_buffer(strlen(status),
                                                     (void*)status,
                                                     MHD_RESPMEM_PERSISTENT);
            MHD_add_response_header(response, "Content-Type", "application/json");
            ret = MHD_queue_response(connection, result == 0 ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST, response);
            MHD_destroy_response(response);
            return ret;
        }
        return MHD_YES;
    }
    
    // Неизвестный URL
    response = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
    MHD_destroy_response(response);
    return ret;
}

// Инициализация API сервера
int ai_api_init(KolibriAI* ai, uint16_t port) {
    if (!ai) return -1;
    global_ai = ai;
    
    http_daemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION,
                                 port,
                                 NULL, NULL,
                                 &handle_request, NULL,
                                 MHD_OPTION_END);
    
    return http_daemon ? 0 : -1;
}

// Остановка API сервера
void ai_api_stop(void) {
    if (http_daemon) {
        MHD_stop_daemon(http_daemon);
        http_daemon = NULL;
    }
    global_ai = NULL;
}
