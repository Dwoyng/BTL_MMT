#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <netinet/in.h>
#include "protocol/rdt.h"
#include "utils/go_back_end.h"
#include "utils/selective_repeat.h"
#include "utils/stop_n_wait.h"

typedef struct rdt_server_t rdt_server_t;

typedef struct {
    rdt_server_t* (*init)(uint16_t port, uint32_t window_size);
    void (*listen)(rdt_server_t* _this);
    void (*close)(rdt_server_t* _this);
} rdt_server_proc_t;

struct rdt_server_t {
    const rdt_server_proc_t *proc;
};

extern const rdt_server_proc_t server_proc;

#endif