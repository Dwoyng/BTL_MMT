#ifndef UTILS_GO_BACK_N_H
#define UTILS_GO_BACK_N_H

#include <stdint.h>
#include <netinet/in.h>
#include "protocol/rdt.h"

typedef struct go_back_n_t go_back_n_t;


typedef struct {
    go_back_n_t* (*init)(uint32_t window_size, int sockfd, struct sockaddr_in dest_addr);
    int8_t (*deinit)(go_back_n_t *_this);
    int8_t (*send)(go_back_n_t* _this, char *data, uint16_t len);
    int8_t (*handle_ack)(go_back_n_t* _this);
    int8_t (*handle_timeout)(go_back_n_t *_this);
    int8_t (*receive)(go_back_n_t *_this);
} go_back_n_proc_t;

struct go_back_n_t {
    const go_back_n_proc_t *proc;
    void (*data_handler)(char* data, uint16_t len);
};

const go_back_n_proc_t* gbn_get_vtable(void);

#endif