#ifndef A3_POLL_AREA_H_
#define A3_POLL_AREA_H_
#include <cstdint>
#include <cinttypes>
#include "a3.h"
namespace a3 {

class context;

class poll_area_t {
 public:
    struct channel_and_offset_t {
        uint32_t channel;
        uint32_t offset;
    };

    poll_area_t();

    bool in_range(context* ctx, uint64_t offset) const;
    channel_and_offset_t extract_channel_and_offset(context* ctx, uint64_t offset) const;

    void write(context* ctx, const command& cmd);
    uint32_t read(context* ctx, const command& cmd);

    void set_area(uint64_t area) {
        A3_LOG("POLL_AREA 0x%" PRIX64 "\n", area);
        area_ = area;
    }
    uint64_t area() const { return area_; }

 private:
    uint32_t per_size_;
    uint64_t area_;
};

}  // namespace a3
#endif  // A3_POLL_AREA_H_
/* vim: set sw=4 ts=4 et tw=80 : */
