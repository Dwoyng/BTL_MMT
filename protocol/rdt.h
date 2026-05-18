#ifndef PROTOCOL_RDT_H
#define PROTOCOL_RDT_H

#include <stdint.h>
#include <netinet/in.h>

// [FIX #1] Giảm từ 150000 xuống 8192.
// UDP tối đa ~65507 bytes/gói. 150000 bytes khiến sendto() trả về
// EMSGSIZE và không có byte nào thực sự được gửi đi.
#define RDT_PACKAGE_LEN 8192

typedef struct rdt_t rdt_t;
typedef struct rdt_proc_t rdt_proc_t;

struct rdt_proc_t {
    rdt_t* (*init)(void);
    int8_t (*deint)(rdt_t* _this);
    int8_t (*packing)(rdt_t *_this, uint32_t sequence_num, char* data, uint16_t data_len, uint8_t type, uint32_t ack);
    int8_t (*receive)(rdt_t *_this, int sockfd);
    int8_t (*send)(rdt_t* _this, int sockfd, struct sockaddr_in *to_addr);
    uint16_t (*check_sum_calc)(rdt_t* _this);
    uint8_t (*get_state)(rdt_t* _this);
    int8_t (*extract)(rdt_t* _this, char *data_out);
    int8_t (*corrupt)(rdt_t *_this);
    uint32_t (*get_seq)(rdt_t* _this);
    uint32_t (*get_ack)(rdt_t* _this);
    uint16_t (*get_data_len)(rdt_t* _this);
};

struct rdt_t {
    const rdt_proc_t *proc;
};

const rdt_proc_t* rdt_get_vtable(void);

#endif