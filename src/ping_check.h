#ifndef PING_CHECK_H
#define PING_CHECK_H

#include <sys/time.h>

// Проверка доступности узлов через PING с измерением RTT
static void check_peers_availability(void) {
    for (size_t i = 0; i < G.peer_count; i++) {
        int fd;
        int rc = connect_to(G.peers[i].host, G.peers[i].port, &fd);
        if (rc != 0) {
            printf("[PING] Failed to connect to %s:%u\n", 
                   G.peers[i].host, G.peers[i].port);
            continue;
        }
        char ping[128];
        snprintf(ping, sizeof(ping), "PING:%s:%llu", G.id, (unsigned long long)now_ms());
        
        // Сохраняем время отправки
        struct timeval start, end;
        gettimeofday(&start, NULL);

        // Отправляем PING с TTL=3 (максимум 3 прыжка)
        FrameHdr h;
        memset(&h, 0, sizeof(h));
        h.magic = htons(MAGIC);
        h.ver = PROTO_VER;
        h.type = MSG_PING;
        h.ttl = 3;
        h.len = htons((uint16_t)strlen(ping));
        
        if (!send_frame(fd, MSG_PING, 0, ping, (uint16_t)strlen(ping))) {
            printf("[PING] Failed to send PING to %s:%u\n",
                   G.peers[i].host, G.peers[i].port);
            close(fd);
            continue;
        }
        
        // Очищаем буфер перед получением ответа
        unsigned char buf[MAX_PAYLOAD];
        memset(buf, 0, sizeof(buf));
        FrameHdr resp;
        if (!recv_frame(fd, &resp, buf, sizeof(buf))) {
            printf("[PING] Failed to receive response from %s:%u\n",
                   G.peers[i].host, G.peers[i].port);
            close(fd);
            continue;
        }

        // Время получения ответа
        gettimeofday(&end, NULL);
        long rtt_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec)/1000;

        if (resp.type == MSG_PING) {
            printf("[PING] Node %s:%u is alive (TTL=%u, RTT=%ld ms)\n",
                   G.peers[i].host, G.peers[i].port, resp.ttl, rtt_ms);
        }
        close(fd);
    }
}

#endif // PING_CHECK_H
