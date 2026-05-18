#include "selective_repeat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPE_DATA 0
#define TYPE_ACK  1

typedef struct {
    rdt_t* pkg;
    uint8_t is_acked;
} sr_slot_t;

typedef struct {
    selective_repeat_t interface;
    uint32_t send_base;
    uint32_t nextseqnum;
    uint32_t rcv_base;
    uint32_t window_size;
    int sockfd;
    struct sockaddr_in dest_addr;
    sr_slot_t send_window[256];
    sr_slot_t rcv_window[256];
} sr_impl_t;

#define _impl(x) ((sr_impl_t*)(x))

static selective_repeat_t* sr_init(uint32_t window_size, int sockfd, struct sockaddr_in dest_addr);
static int8_t sr_deinit(selective_repeat_t *_this);
static int8_t sr_send(selective_repeat_t* _this, char *data, uint16_t len);
static int8_t sr_handle_ack(selective_repeat_t* _this);
static int8_t sr_handle_timeout(selective_repeat_t *_this, uint32_t seq_num);
static int8_t sr_receive(selective_repeat_t *_this);

static const selective_repeat_proc_t sr_vtable = {
    .init           = sr_init,
    .deinit         = sr_deinit,
    .send           = sr_send,
    .handle_ack     = sr_handle_ack,
    .handle_timeout = sr_handle_timeout,
    .receive        = sr_receive
};

const selective_repeat_proc_t* sr_get_vtable(void) { return &sr_vtable; }

static selective_repeat_t* sr_init(uint32_t window_size, int sockfd,
                                    struct sockaddr_in dest_addr) {
    sr_impl_t* impl = (sr_impl_t*)malloc(sizeof(sr_impl_t));
    if (!impl) return NULL;

    impl->interface.proc = &sr_vtable;
    impl->send_base  = 1;
    impl->nextseqnum = 1;
    impl->rcv_base   = 1;
    impl->window_size = window_size;
    impl->sockfd     = sockfd;
    impl->dest_addr  = dest_addr;
    memset(impl->send_window, 0, sizeof(impl->send_window));
    memset(impl->rcv_window,  0, sizeof(impl->rcv_window));
    return (selective_repeat_t*)impl;
}

static int8_t sr_deinit(selective_repeat_t *_this) {
    if (!_this) return -1;
    sr_impl_t* impl = _impl(_this);
    const rdt_proc_t* rdt = rdt_get_vtable();
    for (int i = 0; i < 256; i++) {
        if (impl->send_window[i].pkg) rdt->deint(impl->send_window[i].pkg);
        if (impl->rcv_window[i].pkg)  rdt->deint(impl->rcv_window[i].pkg);
    }
    free(impl);
    return 0;
}

static int8_t sr_send(selective_repeat_t* _this, char *data, uint16_t len) {
    sr_impl_t* impl = _impl(_this);
    const rdt_proc_t* rdt = rdt_get_vtable();

    if (impl->nextseqnum < impl->send_base + impl->window_size) {
        uint32_t idx = impl->nextseqnum % 256;

        if (impl->send_window[idx].pkg) {
            rdt->deint(impl->send_window[idx].pkg);
        }
        impl->send_window[idx].pkg      = rdt->init();
        impl->send_window[idx].is_acked = 0;

        rdt_t* p = impl->send_window[idx].pkg;
        p->proc->packing(p, impl->nextseqnum, data, len, TYPE_DATA, 0);
        if (p->proc->send(p, impl->sockfd, &impl->dest_addr) < 0) {
            printf("[SR] Send failed for seq=%d\n", impl->nextseqnum);
            return -1;
        }
        printf("[SR] Sent seq=%d\n", impl->nextseqnum);
        impl->nextseqnum++;
        return 0;
    }
    return -1;
}

// [FIX #4] Trước đây hardcode ack_num = 1 — không có ý nghĩa gì.
// Sau fix: đọc số ACK thực tế từ header gói nhận bằng get_ack().
static int8_t sr_handle_ack(selective_repeat_t* _this) {
    sr_impl_t* impl = _impl(_this);
    const rdt_proc_t* rdt = rdt_get_vtable();

    rdt_t *tmp = rdt->init();
    if (tmp->proc->receive(tmp, impl->sockfd) == 0 && !tmp->proc->corrupt(tmp)) {
        // [FIX #4] Đọc ack_num từ header, không hardcode
        uint32_t ack_num = tmp->proc->get_ack(tmp);
        uint32_t idx = ack_num % 256;

        if (ack_num >= impl->send_base &&
            ack_num < impl->send_base + impl->window_size &&
            impl->send_window[idx].pkg) {

            impl->send_window[idx].is_acked = 1;
            printf("[SR] ACK %d received\n", ack_num);

            // Trượt cửa sổ gửi: giải phóng các slot đã được ACK liên tiếp từ send_base
            while (impl->send_base < impl->nextseqnum &&
                   impl->send_window[impl->send_base % 256].is_acked) {
                uint32_t bidx = impl->send_base % 256;
                rdt->deint(impl->send_window[bidx].pkg);
                impl->send_window[bidx].pkg      = NULL;
                impl->send_window[bidx].is_acked = 0;
                impl->send_base++;
                printf("[SR] Window slided to send_base=%d\n", impl->send_base);
            }
        }
    }
    tmp->proc->deint(tmp);
    return 0;
}

static int8_t sr_handle_timeout(selective_repeat_t *_this, uint32_t seq_num) {
    sr_impl_t* impl = _impl(_this);
    uint32_t idx = seq_num % 256;
    rdt_t* p = impl->send_window[idx].pkg;
    if (p && !impl->send_window[idx].is_acked) {
        printf("[SR] Timeout: resending seq=%d\n", seq_num);
        p->proc->send(p, impl->sockfd, &impl->dest_addr);
    }
    return 0;
}

// [FIX #5] Trước đây hardcode seq = 1 — luôn xử lý gói đầu tiên.
// Sau fix: đọc seq thực tế từ header bằng get_seq().
static int8_t sr_receive(selective_repeat_t *_this) {
    sr_impl_t* impl = _impl(_this);
    const rdt_proc_t* rdt = rdt_get_vtable();
    rdt_t *rcv = rdt->init();

    if (rcv->proc->receive(rcv, impl->sockfd) == 0 && !rcv->proc->corrupt(rcv)) {
        // [FIX #5] Đọc seq thực tế từ header, không hardcode
        uint32_t seq = rcv->proc->get_seq(rcv);
        uint32_t idx = seq % 256;

        if (seq >= impl->rcv_base && seq < impl->rcv_base + impl->window_size) {
            // Gửi ACK cho gói này dù có thể đã nhận trước đó (SR buffer)
            rdt_t *ack = rdt->init();
            ack->proc->packing(ack, 0, NULL, 0, TYPE_ACK, seq);
            ack->proc->send(ack, impl->sockfd, &impl->dest_addr);
            ack->proc->deint(ack);

            if (!impl->rcv_window[idx].pkg) {
                // Lưu gói vào buffer nhận, rcv sẽ được deint khi deliver
                impl->rcv_window[idx].pkg      = rcv;
                impl->rcv_window[idx].is_acked = 1;
                rcv = NULL; // rcv đã được transfer cho rcv_window, đừng deint bên dưới

                printf("[SR] Buffered seq=%d\n", seq);

                // Trượt cửa sổ nhận: deliver các gói liên tiếp từ rcv_base
                while (impl->rcv_window[impl->rcv_base % 256].pkg != NULL) {
                    uint32_t cidx = impl->rcv_base % 256;
                    rdt_t *ready  = impl->rcv_window[cidx].pkg;
                    char app_buf[RDT_PACKAGE_LEN];
                    ready->proc->extract(ready, app_buf);
                    printf("[SR] Deliver seq=%d to App\n", impl->rcv_base);
                    ready->proc->deint(ready);
                    impl->rcv_window[cidx].pkg      = NULL;
                    impl->rcv_window[cidx].is_acked = 0;
                    impl->rcv_base++;
                    printf("[SR] New rcv_base=%d\n", impl->rcv_base);
                }
            }
            // Gói đã có trong buffer (duplicate) — ACK đã gửi rồi, bỏ qua
        }
        // Ngoài cửa sổ: bỏ qua hoặc gửi lại ACK cũ
    }
    // Chỉ deint nếu rcv chưa được transfer cho rcv_window
    if (rcv) rcv->proc->deint(rcv);
    return 0;
}