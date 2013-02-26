#ifndef HW_NVC0_NVC0_FIFO_H_
#define HW_NVC0_NVC0_FIFO_H_
#include "nvc0.h"
namespace nvc0 {
class context;

void fifo_playlist_update(context* ctx, uint64_t vmaddr, uint32_t count);

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_FIFO_H_
/* vim: set sw=4 ts=4 et tw=80 : */
