#ifndef A3_POLL_AREA_H_
#define A3_POLL_AREA_H_
#include <stdint.h>
#include "a3.h"
namespace a3 {

class context;

class poll_area_t {
 public:
    static const int kMaxSize = A3_CHANNELS * 0x1000UL;

    struct channel_and_offset_t {
        uint32_t channel;
        uint32_t offset;
    };

    static bool in_poll_area(context* ctx, uint64_t offset);
    static channel_and_offset_t extract_channel_and_offset(context* ctx, uint64_t offset);
};

}  // namespace a3
#endif  // A3_POLL_AREA_H_
/* vim: set sw=4 ts=4 et tw=80 : */
