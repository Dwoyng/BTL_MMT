#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>


typedef struct {
    go_back_n_t interface;
    uint32_t base;
    uint32_t nextseqnum;
    uint32_t window_size;
    uint32_t expectedseqnum;
    int sockfd;
    struct sockaddr_in dest_addr;
    rdt_t* sndpkt[256];
} gbn_local_impl_t;

typedef struct {
    int sockfd;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    go_back_n_t *gbn;
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
    impl->sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&impl->server_addr, 0, sizeof(impl->server_addr));
    impl->server_addr.sin_family      = AF_INET;
    impl->server_addr.sin_addr.s_addr = INADDR_ANY;
    impl->server_addr.sin_port        = htons(port);

    if (bind(impl->sockfd, (struct sockaddr *)&impl->server_addr,
             sizeof(impl->server_addr)) < 0) {
        perror("[Server] bind failed");
        free(impl);
        return NULL;
    }

    memset(&impl->client_addr, 0, sizeof(impl->client_addr));
    impl->gbn        = gbn_get_vtable()->init(window_size, impl->sockfd, impl->client_addr);
    impl->is_running = 1;

    g_output_file = fopen("received_output.bin", "wb");
    if (!g_output_file) {
        perror("[Server] Cannot open output file");
    }
    impl->gbn->data_handler = server_data_handler;

    return (rdt_server_t*)impl;
}

static void server_listen(rdt_server_t* _this) {
    server_impl_t* impl = _impl(_this);
    printf("[Server] Listening on port 8080...\n");
    printf("[Server] Writing received data to: received_output.bin\n");

    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);
    char peek_buf[64];

    gbn_local_impl_t* gbn_core = (gbn_local_impl_t*)impl->gbn;

    while (impl->is_running) {
        ssize_t len = recvfrom(impl->sockfd, peek_buf, sizeof(peek_buf),
                               MSG_PEEK,
                               (struct sockaddr *)&src_addr, &addr_len);
        if (len > 0) {
            if (gbn_core) {
                gbn_core->dest_addr = src_addr;
            }

            impl->gbn->proc->receive(impl->gbn);
        }
        usleep(500);
    }

    // Đóng file khi server dừng
    if (g_output_file) {
        fclose(g_output_file);
        g_output_file = NULL;
        printf("[Server] Output file closed.\n");
    }
}

const rdt_server_proc_t server_proc = {
    .init   = server_init,
    .listen = server_listen
};

int main() {
    rdt_server_t* server = server_proc.init(8080, 4);
    if (server) {
        server_proc.listen(server);
    }
    return 0;
}