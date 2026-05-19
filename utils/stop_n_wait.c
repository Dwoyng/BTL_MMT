#include "stop_n_wait.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TYPE_DATA 0
#define TYPE_ACK  1

typedef struct {
    stop_n_wait_t interface;
    uint32_t seq;
    uint32_t expected_seq;
    int sockfd;
    struct sockaddr_in dest_addr;
    rdt_t* last_pkg;
    uint8_t can_send;
    uint32_t window_size;
} snw_impl_t;

#define _impl(x) ((snw_impl_t*)(x))

static stop_n_wait_t* snw_init(int sockfd, struct sockaddr_in dest_addr);
static int8_t snw_deinit(stop_n_wait_t *_this);
static int8_t snw_send(stop_n_wait_t* _this, char *data, uint16_t len);
static int8_t snw_handle_ack(stop_n_wait_t* _this);
static int8_t snw_handle_timeout(stop_n_wait_t *_this);
static int8_t snw_receive(stop_n_wait_t *_this);

static const stop_n_wait_proc_t snw_vtable = {
    .init           = snw_init,
    .deinit         = snw_deinit,
    .send           = snw_send,
    .handle_ack     = snw_handle_ack,
    .handle_timeout = snw_handle_timeout,
    .receive        = snw_receive
};

const stop_n_wait_proc_t* snw_get_vtable(void) { return &snw_vtable; }

static stop_n_wait_t* snw_init(int sockfd, struct sockaddr_in dest_addr) {
    snw_impl_t* impl = (snw_impl_t*)malloc(sizeof(snw_impl_t));
    if (!impl) return NULL;
    impl->interface.proc = &snw_vtable;
    impl->seq          = 0;
    impl->expected_seq = 0;
    impl->sockfd       = sockfd;
    impl->dest_addr    = dest_addr;
    impl->last_pkg     = NULL;
    impl->can_send     = 1;
    return (stop_n_wait_t*)impl;
}

static int8_t snw_deinit(stop_n_wait_t *_this) {
    if (!_this) return -1;
    snw_impl_t* impl = _impl(_this);
    if (impl->last_pkg) impl->last_pkg->proc->deint(impl->last_pkg);
    free(impl);
    return 0;
}

static int8_t snw_send(stop_n_wait_t* _this, char *data, uint16_t len) {
    snw_impl_t* impl = _impl(_this);
    const rdt_proc_t* rdt = rdt_get_vtable();

    if (impl->can_send) {
        if (impl->last_pkg) impl->last_pkg->proc->deint(impl->last_pkg);
        impl->last_pkg = rdt->init();
        rdt_t* p = impl->last_pkg;
        p->proc->packing(p, impl->seq, data, len, TYPE_DATA, 0);
        if (p->proc->send(p, impl->sockfd, &impl->dest_addr) < 0) {
            printf("[SnW] Send failed for seq=%d\n", impl->seq);
            return -1;
        }
        impl->can_send = 0;
        printf("[SnW] Packet seq=%d sent. Waiting for ACK...\n", impl->seq);
        return 0;
    }
    return -1;
}


static int8_t snw_handle_ack(stop_n_wait_t* _this) {
    snw_impl_t* impl = _impl(_this);
    const rdt_proc_t* rdt = rdt_get_vtable();

    rdt_t *tmp = rdt->init();
    if (tmp->proc->receive(tmp, impl->sockfd) == 0 && !tmp->proc->corrupt(tmp)) {

        uint32_t ack_num = tmp->proc->get_ack(tmp);

        if (ack_num == impl->seq) {
            printf("[SnW] Correct ACK %d received for packet %d\n",
                   ack_num, impl->seq);
            impl->can_send = 1;
            impl->seq = 1 - impl->seq; // Toggle 0 ↔ 1
        } else {
            printf("[SnW] Wrong ACK %d received (expected %d), ignoring\n",
                   ack_num, impl->seq);
        }
    }
    tmp->proc->deint(tmp);
    return 0;
}

static int8_t snw_handle_timeout(stop_n_wait_t *_this) {
    snw_impl_t* impl = _impl(_this);
    if (impl->last_pkg && !impl->can_send) {
        printf("[SnW] Timeout! Resending packet seq=%d\n", impl->seq);
        impl->last_pkg->proc->send(impl->last_pkg, impl->sockfd, &impl->dest_addr);
    }
    return 0;
}


static int8_t snw_receive(stop_n_wait_t *_this) {
    snw_impl_t* impl = _impl(_this);
    const rdt_proc_t* rdt = rdt_get_vtable();
    rdt_t *rcv = rdt->init();

    if (rcv->proc->receive(rcv, impl->sockfd) == 0) {
        if (!rcv->proc->corrupt(rcv)) {

            uint32_t rcv_seq = rcv->proc->get_seq(rcv);

            if (rcv_seq == impl->expected_seq) {
                char buf[RDT_PACKAGE_LEN];
                rcv->proc->extract(rcv, buf);
                printf("[SnW] Deliver packet seq=%d to App.\n", rcv_seq);

                rdt_t *ack = rdt->init();
                ack->proc->packing(ack, 0, NULL, 0, TYPE_ACK, impl->expected_seq);
                ack->proc->send(ack, impl->sockfd, &impl->dest_addr);
                ack->proc->deint(ack);

                impl->expected_seq = 1 - impl->expected_seq; // Toggle 0 ↔ 1
            } else {

                printf("[SnW] Duplicate/wrong seq=%d (expected=%d), re-ACK\n",
                       rcv_seq, impl->expected_seq);
                rdt_t *ack = rdt->init();
                ack->proc->packing(ack, 0, NULL, 0, TYPE_ACK,
                                   1 - impl->expected_seq);
                ack->proc->send(ack, impl->sockfd, &impl->dest_addr);
                ack->proc->deint(ack);
            }
        } else {
            printf("[SnW] Corrupted packet received.\n");
        }
    }
    rcv->proc->deint(rcv);
    return 0;
}