#ifndef UTILS_STOP_N_WAIT_H
#define UTILS_STOP_N_WAIT_H

#include <stdint.h>
#include <netinet/in.h>
#include "protocol/rdt.h"

typedef struct stop_n_wait_t stop_n_wait_t;

typedef struct {

    stop_n_wait_t* (*init)(int sockfd, struct sockaddr_in dest_addr);

    int8_t (*deinit)(stop_n_wait_t *_this);
    // Gửi dữ liệu (Phía Sender)
    int8_t (*send)(stop_n_wait_t* _this, char *data, uint16_t len);

    int8_t (*handle_ack)(stop_n_wait_t* _this);

    int8_t (*handle_timeout)(stop_n_wait_t *_this);

    int8_t (*receive)(stop_n_wait_t *_this);
} stop_n_wait_proc_t;

struct stop_n_wait_t {
    const stop_n_wait_proc_t *proc;
};

// Entry point duy nhất để lấy bảng hàm SnW
const stop_n_wait_proc_t* snw_get_vtable(void);

#endif