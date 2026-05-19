#include "client.h"
#include "utils/porting.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

// Định nghĩa cấu trúc Client chuẩn chỉnh theo đúng yêu cầu hệ thống
typedef struct {
    int sockfd;
    struct sockaddr_in server_addr;
    protocol_handle protocol; // Hứng con trỏ từ porting.h
    uint8_t is_running;
    rdt_t* pkg;
} client_impl_t;

#define _impl(x) ((client_impl_t*)(x))

static rdt_client_t* client_init(const char* ip, uint16_t port, uint32_t window_size) {
    client_impl_t* impl = (client_impl_t*)malloc(sizeof(client_impl_t));
    if (!impl) return NULL;

    impl->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 10000; // Timeout 10ms để tránh treo nghẽn khi lặp nhận ACK
    setsockopt(impl->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&impl->server_addr, 0, sizeof(impl->server_addr));
    impl->server_addr.sin_family = AF_INET;
    impl->server_addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &impl->server_addr.sin_addr);

    impl->pkg = rdt_get_vtable()->init();

    // Khởi tạo và nạp cấu hình chuẩn theo từng loại giao thức
#if (CHOSEN_PROTOCOL == GO_BACK_END)
    impl->protocol = (protocol_handle)gbn_get_vtable()->init(window_size, impl->sockfd, impl->server_addr);
    ((struct go_back_n_t*)(impl->protocol))->data_handler = NULL;
#elif (CHOSEN_PROTOCOL == SELECTIVE_REPEAT)
    impl->protocol = (protocol_handle)sr_get_vtable()->init(window_size, impl->sockfd, impl->server_addr);
#else
    // Gỡ lỗi signature hàm init của Stop-and-Wait chỉ nhận 2 tham số như file .h của mày
    stop_n_wait_t* (*snw_init_ptr)(int, struct sockaddr_in) = snw_get_vtable()->init;
    impl->protocol = (protocol_handle)snw_init_ptr(impl->sockfd, impl->server_addr);
#endif

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

    // Trích xuất con trỏ hàm thông qua cấu trúc vtable gốc
    void ***vtable = (void ***)(impl->protocol);
    int8_t (*proto_send)(void*, char*, uint16_t) = (int8_t (*)(void*, char*, uint16_t))vtable[0][2]; 
    int8_t (*proto_ack)(void*)                   = (int8_t (*)(void*))vtable[0][3];                
    int8_t (*proto_timeout)(void*)               = (int8_t (*)(void*))vtable[0][4];

    // ÁNH XẠ TRỰC TIẾP QUA STRUCT CỤC BỘ: Ép compiler tự tính toán offset chính xác theo byte alignment của máy
#if (CHOSEN_PROTOCOL == GO_BACK_END)
    typedef struct { void* proc; void* dh; uint32_t base; uint32_t nextseqnum; uint32_t window_size; } gbn_local_core;
    gbn_local_core *gbn = (gbn_local_core*)impl->protocol;
    uint32_t *gbn_base = &gbn->base;
    uint32_t *gbn_next = &gbn->nextseqnum;
    uint32_t *gbn_win  = &gbn->window_size;
#elif (CHOSEN_PROTOCOL == SELECTIVE_REPEAT)
    typedef struct { void* proc; uint32_t send_base; uint32_t nextseqnum; uint32_t rcv_base; uint32_t window_size; } sr_local_core;
    sr_local_core *sr = (sr_local_core*)impl->protocol;
    uint32_t *sr_base = &sr->send_base;
    uint32_t *sr_next = &sr->nextseqnum;
    uint32_t *sr_win  = &sr->window_size;
#else
    // Định nghĩa khớp 100% với file stop_n_wait.c thực tế của mày vừa đẩy lên để bốc can_send an toàn
    typedef struct { void* proc; uint32_t seq; uint32_t expected_seq; int sockfd; struct sockaddr_in dest_addr; rdt_t* last_pkg; uint8_t can_send; uint32_t window_size; } snw_local_core;
    snw_local_core *snw = (snw_local_core*)impl->protocol;
    uint8_t *snw_can_send = &snw->can_send;
    uint32_t *snw_seq     = &snw->seq;
#endif

    while ((read_bytes = (uint16_t)fread(buffer, 1, RDT_PACKAGE_LEN, f)) > 0) {
        int timeout_counter = 0;

        // Vòng lặp chặn dựa trên biến trạng thái chính xác và an toàn của từng cấu trúc
#if (CHOSEN_PROTOCOL == GO_BACK_END)
        while (*gbn_next >= *gbn_base + *gbn_win) {
            proto_ack(impl->protocol);
            usleep(1000);
            if (++timeout_counter >= 50) { // Quá 50ms kẹt cửa sổ -> Kích hoạt re-send
                proto_timeout(impl->protocol);
                timeout_counter = 0;
            }
        }
#elif (CHOSEN_PROTOCOL == SELECTIVE_REPEAT)
        while (*sr_next >= *sr_base + *sr_win) {
            proto_ack(impl->protocol);
            usleep(1000);
            if (++timeout_counter >= 50) {
                // Với Selective Repeat hàm timeout nhận thêm tham số seq_num, ép kiểu gọi chính xác
                int8_t (*sr_timeout_ptr)(void*, uint32_t) = (int8_t (*)(void*, uint32_t))vtable[0][4];
                sr_timeout_ptr(impl->protocol, *sr_base);
                timeout_counter = 0;
            }
        }
#else
        // FIX DEADLOCK CHO STOP-AND-WAIT: Chống kẹt vô hạn khi rụng gói hoặc mất ACK dọc đường
        while (*snw_can_send == 0) {
            proto_ack(impl->protocol);
            usleep(1000);
            if (++timeout_counter >= 30) { // Quá 30ms không nhận được ACK -> Gọi handle_timeout truyền lại gói seq cũ
                printf("[Client] [SnW] Không nhận được ACK gói seq=%d quá 30ms! Kích hoạt truyền lại...\n", *snw_seq);
                proto_timeout(impl->protocol);
                timeout_counter = 0;
            }
        }
#endif

        // Thực hiện đẩy gói dữ liệu đi
        while (proto_send(impl->protocol, buffer, read_bytes) != 0) {
            proto_ack(impl->protocol);
            usleep(1000);
        }
        proto_ack(impl->protocol);
    }

    if (ferror(f)) {
        perror("[Client] fread error");
    }

    printf("[Client] Data sent completely. Finalizing window flush...\n");
    
    // Vòng lặp dọn rác cuối: Đảm bảo các gói tin cuối cùng được quét sạch ACK trước khi ngắt socket
    int final_timeout = 0;
#if (CHOSEN_PROTOCOL == GO_BACK_END)
    while (*gbn_base < *gbn_next) { 
        proto_ack(impl->protocol); usleep(1000); 
        if (++final_timeout >= 100) { proto_timeout(impl->protocol); final_timeout = 0; }
    }
#elif (CHOSEN_PROTOCOL == SELECTIVE_REPEAT)
    while (*sr_base < *sr_next) { 
        proto_ack(impl->protocol); usleep(1000); 
        if (++final_timeout >= 100) {
            int8_t (*sr_timeout_ptr)(void*, uint32_t) = (int8_t (*)(void*, uint32_t))vtable[0][4];
            sr_timeout_ptr(impl->protocol, *sr_base);
            final_timeout = 0;
        }
    }
#else
    while (*snw_can_send == 0) { 
        proto_ack(impl->protocol); usleep(1000); 
        if (++final_timeout >= 100) { proto_timeout(impl->protocol); final_timeout = 0; }
    }
#endif

    fclose(f);
    printf("[Client] Transfer execution complete.\n");
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

#if (CHOSEN_PROTOCOL == GO_BACK_END)
    printf("[Client] HỆ THỐNG KHỞI ĐỘNG CƠ CHẾ: GO-BACK-N\n");
#elif (CHOSEN_PROTOCOL == SELECTIVE_REPEAT)
    printf("[Client] HỆ THỐNG KHỞI ĐỘNG CƠ CHẾ: SELECTIVE REPEAT\n");
#else
    printf("[Client] HỆ THỐNG KHỞI ĐỘNG CƠ CHẾ: STOP AND WAIT\n");
#endif

    client_proc.send_file(client, argv[1]);
    return 0;
}