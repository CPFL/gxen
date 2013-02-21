#ifndef HW_NVC0_NVC0_FIFO_H_
#define HW_NVC0_NVC0_FIFO_H_
#include "nvc0.h"

#ifdef __cpp
extern "C" {
#endif

void nvc0_fifo_playlist_update(nvc0_state_t* state, uint64_t vmaddr, uint32_t count);

#ifdef __cpp
}
#endif

#endif  // HW_NVC0_NVC0_FIFO_H_
/* vim: set sw=4 ts=4 et tw=80 : */
