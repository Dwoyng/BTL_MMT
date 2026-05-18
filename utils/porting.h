#ifndef PORTING_H
#define PORTING_H

#ifdef __cplusplus
extern "C" {
#endif
#include "go_back_end.h"
#include "selective_repeat.h"
#include "stop_n_wait.h"

enum {
    GO_BACK_END = 0,
    SELECTIVE_REPEAT,
    STOP_AND_WAIT,
};

#define CHOSEN_PROTOCOL GO_BACK_END

#if (CHOSEN_PROTOCOL == GO_BACK_END)
#define GET_PROTOCOL_VTABLE()  *go_back_n_t


#elif (CHOSEN_PROTOCOL == SELECTIVE_REPEAT)
#define GET_PROTOCOL_VTABLE()  *selective_repeat_t


#else 
#define GET_PROTOCOL_VTABLE()  *stop_n_wait_t

#endif

#ifdef __cplusplus
}
#endif



#endif