#include "client.h"
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
    go_back_n_t *gbn;
    uint8_t is_running;
    rdt_t* pkg;
} client_impl_t;

#define _impl(x) ((client_impl_t*)(x))

static rdt_client_t* client_init(const char* ip, uint16_t port, uint32_t window_size) {
    client_impl_t* impl = (client_impl_t*)malloc(sizeof(client_impl_t));
    impl->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 10000; 
    setsockopt(impl->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&impl->server_addr, 0, sizeof(impl->server_addr));
    impl->server_addr.sin_family = AF_INET;
    impl->server_addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &impl->server_addr.sin_addr);

    impl->pkg = rdt_get_vtable()->init();
    impl->gbn = gbn_get_vtable()->init(window_size, impl->sockfd, impl->server_addr);
    impl->gbn->data_handler = NULL;

    gbn_local_impl_t* gbn_core = (gbn_local_impl_t*)impl->gbn;
    if (gbn_core) {
        gbn_core->dest_addr = impl->server_addr;
        gbn_core->sockfd    = impl->sockfd;
    }

    impl->is_running = 1;
    return (rdt_client_t*)impl;
}

static int8_t client_send_file(rdt_client_t* _this, const char* filepath) {
    client_impl_t* impl = _impl(_this);
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        perror("[Client] Cannot open file");
        return -1;
    }

    char buffer[RDT_PACKAGE_LEN];
    uint16_t read_bytes;
    gbn_local_impl_t* gbn = (gbn_local_impl_t*)impl->gbn;
    gbn->dest_addr = impl->server_addr;

    while ((read_bytes = (uint16_t)fread(buffer, 1, RDT_PACKAGE_LEN, f)) > 0) {
        while (gbn->nextseqnum >= gbn->base + gbn->window_size) {
            impl->gbn->proc->handle_ack(impl->gbn);
            usleep(1000);
        }
        gbn->dest_addr = impl->server_addr;

        int8_t ret = impl->gbn->proc->send(impl->gbn, buffer, read_bytes);
        if (ret == 0) {

            impl->gbn->proc->handle_ack(impl->gbn);
        } else {
            usleep(1000);
        }
    }

    if (ferror(f)) {
        perror("[Client] fread error");
    }

    printf("[Client] File sent, waiting for all ACKs...\n");
    while (gbn->base < gbn->nextseqnum) {
        impl->gbn->proc->handle_ack(impl->gbn);
        usleep(2000);
    }

    fclose(f);
    printf("[Client] All packets acknowledged. Transfer complete.\n");
    return 0;
}

const rdt_client_proc_t client_proc = {
    .init      = client_init,
    .send_file = client_send_file
};

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    rdt_client_t* client = client_proc.init("127.0.0.1", 8080, 4);
    printf("[Client] Started, sending file: %s\n", argv[1]);

    if (client_proc.send_file(client, argv[1]) == 0) {
        printf("[Client] Send completed successfully.\n");
    }

    sleep(1);
    return 0;
}