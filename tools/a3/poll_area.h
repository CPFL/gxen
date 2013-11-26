#ifndef A3_POLL_AREA_H_
#define A3_POLL_AREA_H_
#include <cstdint>
#include "a3.h"
namespace a3 {

class context;

class poll_area_t {
 public:
    struct channel_and_offset_t {
        uint32_t channel;
        uint32_t offset;
    };

    static bool in_poll_area(context* ctx, uint64_t offset);
    static channel_and_offset_t extract_channel_and_offset(context* ctx, uint64_t offset);

    std::size_t size() const { return A3_BAR1_POLL_AREA_SIZE; }

    void write(context* ctx, const command& cmd);
    uint32_t read(context* ctx, const command& cmd);
};

}  // namespace a3
#endif  // A3_POLL_AREA_H_
/* vim: set sw=4 ts=4 et tw=80 : */
