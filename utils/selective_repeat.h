#ifndef UTILS_SELECTIVE_REPEAT_H
#define UTILS_SELECTIVE_REPEAT_H

#include <stdint.h>
#include <netinet/in.h>
#include "protocol/rdt.h"

typedef struct selective_repeat_t selective_repeat_t;

typedef struct {

    selective_repeat_t* (*init)(uint32_t window_size, int sockfd, struct sockaddr_in dest_addr);

    int8_t (*deinit)(selective_repeat_t *_this);

    int8_t (*send)(selective_repeat_t* _this, char *data, uint16_t len);

    int8_t (*handle_ack)(selective_repeat_t* _this);

    int8_t (*handle_timeout)(selective_repeat_t *_this, uint32_t seq_num);

    int8_t (*receive)(selective_repeat_t *_this);
} selective_repeat_proc_t;

struct selective_repeat_t {
    const selective_repeat_proc_t *proc;
};

// Entry point để lấy bảng hàm SR
const selective_repeat_proc_t* sr_get_vtable(void);

#endif