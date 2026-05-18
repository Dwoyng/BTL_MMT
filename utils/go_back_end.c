#include "go_back_end.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPE_DATA 0
#define TYPE_ACK  1

typedef struct {
    go_back_n_t interface;
    uint32_t base;
    uint32_t nextseqnum;
    uint32_t window_size;
    uint32_t expectedseqnum;
    int sockfd;
    struct sockaddr_in dest_addr;
    rdt_t* sndpkt[256];
} gbn_impl_t;

#define _impl(x) ((gbn_impl_t*)(x))

static go_back_n_t* gbn_init(uint32_t window_size, int sockfd, struct sockaddr_in dest_addr);
static int8_t gbn_deinit(go_back_n_t *_this);
static int8_t gbn_send(go_back_n_t* _this, char *data, uint16_t len);
static int8_t gbn_handle_ack(go_back_n_t* _this);
static int8_t gbn_handle_timeout(go_back_n_t *_this);
static int8_t gbn_receive(go_back_n_t *_this);

static const go_back_n_proc_t gbn_vtable = {
    .init           = gbn_init,
    .deinit         = gbn_deinit,
    .send           = gbn_send,
    .handle_ack     = gbn_handle_ack,
    .handle_timeout = gbn_handle_timeout,
    .receive        = gbn_receive
};

const go_back_n_proc_t* gbn_get_vtable(void) {
    return &gbn_vtable;
}

static go_back_n_t* gbn_init(uint32_t window_size, int sockfd, struct sockaddr_in dest_addr) {
    gbn_impl_t* impl = (gbn_impl_t*)malloc(sizeof(gbn_impl_t));
    if (!impl) return NULL;

    impl->interface.proc         = &gbn_vtable;
    // [FIX #8] Khởi tạo data_handler = NULL, server sẽ gán sau khi init()
    impl->interface.data_handler = NULL;
    impl->base                   = 1;
    impl->nextseqnum             = 1;
    impl->expectedseqnum         = 1;
    impl->window_size            = window_size;
    impl->sockfd                 = sockfd;
    impl->dest_addr              = dest_addr;
    memset(impl->sndpkt, 0, sizeof(impl->sndpkt));

    return (go_back_n_t*)impl;
}

static int8_t gbn_deinit(go_back_n_t *_this) {
    if (!_this) return -1;
    gbn_impl_t* impl = _impl(_this);
    const rdt_proc_t* rdt = rdt_get_vtable();
    for (int i = 0; i < 256; i++) {
        if (impl->sndpkt[i]) rdt->deint(impl->sndpkt[i]);
    }
    free(impl);
    return 0;
}

// [FIX #9] Kiểm tra return value của rdt_send để phát hiện lỗi gửi
static int8_t gbn_send(go_back_n_t* _this, char *data, uint16_t len) {
    gbn_impl_t* impl = _impl(_this);
    const rdt_proc_t* rdt = rdt_get_vtable();

    if (impl->nextseqnum < impl->base + impl->window_size) {
        uint32_t idx = impl->nextseqnum % 256;

        // Giải phóng slot cũ nếu còn (tránh memory leak sau khi wrap-around)
        if (impl->sndpkt[idx]) {
            rdt->deint(impl->sndpkt[idx]);
        }

        impl->sndpkt[idx] = rdt->init();
        rdt_t* pkg = impl->sndpkt[idx];

        pkg->proc->packing(pkg, impl->nextseqnum, data, len, TYPE_DATA, 0);

        // [FIX #9] Kiểm tra lỗi gửi — trước đây bỏ qua hoàn toàn
        if (pkg->proc->send(pkg, impl->sockfd, &impl->dest_addr) < 0) {
            printf("[GBN] SEND ERROR: seq=%d. Kiểm tra kích thước packet và network.\n",
                   impl->nextseqnum);
            return -1;
        }

        printf("[GBN] Sent seq=%d\n", impl->nextseqnum);
        impl->nextseqnum++;
        return 0;
    }

    printf("[GBN] Window full (base=%d, next=%d, size=%d), discard.\n",
           impl->base, impl->nextseqnum, impl->window_size);
    return -1;
}

static int8_t gbn_handle_ack(go_back_n_t* _this) {
    gbn_impl_t* impl = _impl(_this);
    const rdt_proc_t* rdt = rdt_get_vtable();

    rdt_t *tmp_rcv = rdt->init();
    if (tmp_rcv->proc->receive(tmp_rcv, impl->sockfd) == 0) {
        if (!tmp_rcv->proc->corrupt(tmp_rcv)) {
            uint32_t received_ack = tmp_rcv->proc->get_ack(tmp_rcv);
            if (received_ack >= impl->base) {
                printf("[GBN] ACK %d received, window slides to base=%d\n",
                       received_ack, received_ack + 1);
                impl->base = received_ack + 1;
            }
        }
    }
    tmp_rcv->proc->deint(tmp_rcv);
    return 0;
}

static int8_t gbn_handle_timeout(go_back_n_t* _this) {
    gbn_impl_t* impl = _impl(_this);
    printf("[GBN] Timeout! Resending window from base=%d to nextseq=%d\n",
           impl->base, impl->nextseqnum - 1);
    for (uint32_t i = impl->base; i < impl->nextseqnum; i++) {
        rdt_t* pkg = impl->sndpkt[i % 256];
        if (pkg) pkg->proc->send(pkg, impl->sockfd, &impl->dest_addr);
    }
    return 0;
}

// [FIX #3] Kiểm tra seq number nhận được so với expectedseqnum.
// Trước đây: nhận bất kỳ gói nào không lỗi đều ACK — vi phạm GBN.
// Sau fix: chỉ accept gói đúng thứ tự; gói sai thứ tự sẽ bị bỏ
// và re-ACK gói cuối đã nhận thành công (NAK ngầm).
static int8_t gbn_receive(go_back_n_t* _this) {
    gbn_impl_t* impl = _impl(_this);
    const rdt_proc_t* rdt = rdt_get_vtable();
    rdt_t *rcv = rdt->init();

    if (rcv->proc->receive(rcv, impl->sockfd) == 0) {
        if (!rcv->proc->corrupt(rcv)) {
            uint32_t seq  = rcv->proc->get_seq(rcv);
            uint16_t dlen = rcv->proc->get_data_len(rcv);

            if (seq == impl->expectedseqnum) {
                // Gói đúng thứ tự — chuyển lên tầng ứng dụng
                char buffer[RDT_PACKAGE_LEN];
                rcv->proc->extract(rcv, buffer);

                // [FIX #8] Gọi callback nếu tầng trên đã đăng ký
                // (server dùng để ghi file; client không set)
                if (impl->interface.data_handler) {
                    impl->interface.data_handler(buffer, dlen);
                }

                // Gửi ACK cho gói vừa nhận
                rdt_t *ack = rdt->init();
                ack->proc->packing(ack, 0, NULL, 0, TYPE_ACK, impl->expectedseqnum);
                ack->proc->send(ack, impl->sockfd, &impl->dest_addr);
                ack->proc->deint(ack);

                printf("[GBN] Received seq=%d OK, ACK sent, expecting=%d\n",
                       seq, impl->expectedseqnum + 1);
                impl->expectedseqnum++;
            } else {
                // Gói sai thứ tự — GBN yêu cầu bỏ và re-ACK gói cuối hợp lệ
                printf("[GBN] Out-of-order: got seq=%d, expected=%d. Re-ACK %d\n",
                       seq, impl->expectedseqnum, impl->expectedseqnum - 1);
                if (impl->expectedseqnum > 1) {
                    rdt_t *ack = rdt->init();
                    ack->proc->packing(ack, 0, NULL, 0, TYPE_ACK,
                                       impl->expectedseqnum - 1);
                    ack->proc->send(ack, impl->sockfd, &impl->dest_addr);
                    ack->proc->deint(ack);
                }
            }
        } else {
            printf("[GBN] Corrupted packet received, discarded.\n");
        }
    }
    rcv->proc->deint(rcv);
    return 0;
}