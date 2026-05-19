#ifndef CLIENT_CLIENT_H
#define CLIENT_CLIENT_H

#include <stdint.h>
#include <netinet/in.h>
#include "protocol/rdt.h"
#include "utils/go_back_end.h"
#include "utils/selective_repeat.h"
#include "utils/stop_n_wait.h"
#include "utils/porting.h"

typedef struct rdt_client_t rdt_client_t;

typedef struct {

    rdt_client_t* (*init)(const char* ip, uint16_t port, uint32_t window_size);

    int8_t (*send_file)(rdt_client_t* _this, const char* filepath);

    void (*close)(rdt_client_t* _this);
} rdt_client_proc_t;

struct rdt_client_t {
    const rdt_client_proc_t *proc;
};

extern const rdt_client_proc_t client_proc;

#endif