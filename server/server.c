#include "server.h"
#include "utils/porting.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

typedef struct {
    int sockfd;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    protocol_handle protocol; 
    uint8_t is_running;
} server_impl_t;

#define _impl(x) ((server_impl_t*)(x))

static FILE* g_output_file = NULL;

static void server_data_handler(char* data, uint16_t len) {
    if (g_output_file && len > 0) {
        fwrite(data, 1, len, g_output_file);
        fflush(g_output_file);
        printf("[Server] Wrote %u bytes to received_output.bin\n", (unsigned)len);
    }
}

static rdt_server_t* server_init(uint16_t port, uint32_t window_size) {
    server_impl_t* impl = (server_impl_t*)malloc(sizeof(server_impl_t));
    if (!impl) return NULL;
    impl->sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&impl->server_addr, 0, sizeof(impl->server_addr));
    impl->server_addr.sin_family      = AF_INET;
    impl->server_addr.sin_addr.s_addr = INADDR_ANY;
    impl->server_addr.sin_port        = htons(port);

    if (bind(impl->sockfd, (struct sockaddr *)&impl->server_addr, sizeof(impl->server_addr)) < 0) {
        perror("[Server] bind failed");
        free(impl);
        return NULL;
    }

    memset(&impl->client_addr, 0, sizeof(impl->client_addr));
    
#if (CHOSEN_PROTOCOL == GO_BACK_END)
    impl->protocol = (protocol_handle)gbn_get_vtable()->init(window_size, impl->sockfd, impl->client_addr);
    ((struct go_back_n_t*)(impl->protocol))->data_handler = server_data_handler;
#elif (CHOSEN_PROTOCOL == SELECTIVE_REPEAT)
    impl->protocol = (protocol_handle)sr_get_vtable()->init(window_size, impl->sockfd, impl->client_addr);
#else
    stop_n_wait_t* (*snw_init_ptr)(int, struct sockaddr_in) = snw_get_vtable()->init;
    impl->protocol = (protocol_handle)snw_init_ptr(impl->sockfd, impl->client_addr);
#endif

    impl->is_running = 1;
    g_output_file = fopen("received_output.bin", "wb");
    return (rdt_server_t*)impl;
}

static void server_listen(rdt_server_t* _this) {
    server_impl_t* impl = _impl(_this);
    printf("[Server] Listening on port 8080...\n");

    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);
    char peek_buf[64];

    void ***vtable = (void ***)(impl->protocol);
    int8_t (*proto_receive)(void*) = (int8_t (*)(void*))vtable[0][5]; // Index 5 luôn là hàm receive

    // SỬA PHẦN TÍNH OFFSET ĐỊA CHỈ ĐÍCH CHÍNH XÁC:
#if (CHOSEN_PROTOCOL == GO_BACK_END)
    struct sockaddr_in *dest_ptr = (struct sockaddr_in*)((uint8_t*)(impl->protocol) + 36);
#elif (CHOSEN_PROTOCOL == SELECTIVE_REPEAT)
    struct sockaddr_in *dest_ptr = (struct sockaddr_in*)((uint8_t*)(impl->protocol) + 28);
#else
    struct sockaddr_in *dest_ptr = (struct sockaddr_in*)((uint8_t*)(impl->protocol) + 20);
#endif

    while (impl->is_running) {
        ssize_t len = recvfrom(impl->sockfd, peek_buf, sizeof(peek_buf), MSG_PEEK,
                               (struct sockaddr *)&src_addr, &addr_len);
        if (len > 0) {
            if (dest_ptr) {
                *dest_ptr = src_addr; // Cập nhật IP/Port động chuẩn từng byte
            }
            proto_receive(impl->protocol);
        }
        usleep(500);
    }

    if (g_output_file) fclose(g_output_file);
}

const rdt_server_proc_t server_proc = {
    .init   = server_init,
    .listen = server_listen
};

int main() {
    rdt_server_t* server = server_proc.init(8080, 4);
    if (server) server_proc.listen(server);
    return 0;
}