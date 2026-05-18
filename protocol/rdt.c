#include "rdt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

typedef struct {
    uint32_t sequence;
    uint16_t check_sum;
    uint32_t ack;
    uint16_t data_len;
    uint8_t  type;
} __attribute__((packed)) rdt_header_t;

typedef struct {
    rdt_header_t header;
    char msg[RDT_PACKAGE_LEN];
} __attribute__((packed)) rdt_package_t;

typedef struct {
    rdt_t interface;
    rdt_package_t *pkg;
    uint8_t state;
} rdt_impl_t;

#define _impl(x) ((rdt_impl_t*)(x))

static rdt_t* rdt_init(void) {
    rdt_impl_t *impl = (rdt_impl_t*)malloc(sizeof(rdt_impl_t));
    if (!impl) return NULL;
    impl->interface.proc = rdt_get_vtable();
    impl->pkg = (rdt_package_t*)malloc(sizeof(rdt_package_t));
    memset(impl->pkg, 0, sizeof(rdt_package_t));
    impl->state = 0;
    return (rdt_t*)impl;
}

static int8_t rdt_deint(rdt_t* _this) {
    if (!_this) return -1;
    free(_impl(_this)->pkg);
    free(_impl(_this));
    return 0;
}


static uint16_t rdt_check_sum_calc(rdt_t* _this) {
    rdt_impl_t *impl = _impl(_this);
    size_t size = sizeof(rdt_header_t) + ntohs(impl->pkg->header.data_len);
    uint16_t *buf = (uint16_t *)impl->pkg;
    uint32_t sum = 0;
    while (size > 1) { sum += *buf++; size -= 2; }
    if (size > 0) sum += *(uint8_t *)buf;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static int8_t rdt_packing(rdt_t *_this, uint32_t sequence_num, char* data,
                           uint16_t data_len, uint8_t type, uint32_t ack) {
    rdt_impl_t *impl = _impl(_this);
    memset(impl->pkg->msg, 0, RDT_PACKAGE_LEN);

    impl->pkg->header.sequence = htonl(sequence_num);
    impl->pkg->header.ack      = htonl(ack);
    impl->pkg->header.type     = type;
    impl->pkg->header.data_len = htons(data_len);
    if (data && data_len > 0) memcpy(impl->pkg->msg, data, data_len);
    impl->pkg->header.check_sum = 0;
    impl->pkg->header.check_sum = htons(rdt_check_sum_calc(_this));
    return 0;
}


static int8_t rdt_send(rdt_t* _this, int sockfd, struct sockaddr_in *to_addr) {
    rdt_impl_t *impl = _impl(_this);
    size_t send_size = sizeof(rdt_header_t) + ntohs(impl->pkg->header.data_len);
    ssize_t n = sendto(sockfd, impl->pkg, send_size, 0,
                       (struct sockaddr*)to_addr, sizeof(*to_addr));
    if (n < 0) {
        perror("[RDT] sendto failed");
        return -1;
    }
    return 0;
}

static int8_t rdt_receive(rdt_t *_this, int sockfd) {
    rdt_impl_t *impl = _impl(_this);
    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    int n = recvfrom(sockfd, impl->pkg, sizeof(rdt_package_t), 0,
                     (struct sockaddr*)&from, &len);
    return (n > 0) ? 0 : -1;
}

static int8_t rdt_extract(rdt_t* _this, char *data_out) {
    rdt_impl_t *impl = _impl(_this);
    uint16_t dlen = ntohs(impl->pkg->header.data_len);
    memcpy(data_out, impl->pkg->msg, dlen);
    return 0;
}

static int8_t rdt_corrupt(rdt_t *_this) {
    rdt_impl_t *impl = _impl(_this);
    uint16_t sent_sum = ntohs(impl->pkg->header.check_sum);
    impl->pkg->header.check_sum = 0;
    uint16_t calc = rdt_check_sum_calc(_this);
    impl->pkg->header.check_sum = htons(sent_sum);
    return (sent_sum != calc);
}

static uint8_t  rdt_get_state(rdt_t* _this)    { return _impl(_this)->state; }
static uint32_t rdt_get_seq(rdt_t* _this)       { return ntohl(_impl(_this)->pkg->header.sequence); }
static uint32_t rdt_get_ack(rdt_t* _this)       { return ntohl(_impl(_this)->pkg->header.ack); }
static uint16_t rdt_get_data_len(rdt_t* _this)  { return ntohs(_impl(_this)->pkg->header.data_len); }

static const rdt_proc_t rdt_vtable = {
    .init           = rdt_init,
    .deint          = rdt_deint,
    .packing        = rdt_packing,
    .receive        = rdt_receive,
    .send           = rdt_send,
    .check_sum_calc = rdt_check_sum_calc,
    .get_state      = rdt_get_state,
    .extract        = rdt_extract,
    .corrupt        = rdt_corrupt,
    .get_seq        = rdt_get_seq,
    .get_ack        = rdt_get_ack,
    .get_data_len   = rdt_get_data_len,
};

const rdt_proc_t* rdt_get_vtable(void) { return &rdt_vtable; }