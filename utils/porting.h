#ifndef PORTING_H
#define PORTING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "go_back_end.h"
#include "selective_repeat.h"
#include "stop_n_wait.h"


#define GO_BACK_N         0
#define SELECTIVE_REPEAT  1
#define STOP_AND_WAIT     2

#define CHOSEN_PROTOCOL   STOP_AND_WAIT   


#if CHOSEN_PROTOCOL == GO_BACK_N
    typedef go_back_n_t* protocol_handle;

#elif CHOSEN_PROTOCOL == SELECTIVE_REPEAT
    typedef selective_repeat_t* protocol_handle;

#elif CHOSEN_PROTOCOL == STOP_AND_WAIT
    typedef stop_n_wait_t* protocol_handle;

#else
    #error "CHOSEN_PROTOCOL is not defined correctly!"
#endif

#ifdef __cplusplus
}
#endif
#endif