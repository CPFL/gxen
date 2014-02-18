#ifndef A3_PFIFO_H_
#define A3_PFIFO_H_
#include <cstdint>
#include "a3.h"
namespace a3 {

class device_t;
class context;

class pfifo_t {
 public:
    pfifo_t();
    inline uint32_t channels() const { return channels_; }
    bool in_range(uint32_t offset) const;
    void write(context* ctx, command cmd);
    uint32_t read(context* ctx, command cmd);

 private:
    inline uint32_t total_channels() const { return total_channels_; }
    inline uint32_t range() const { return range_; }

    uint32_t total_channels_;
    uint32_t channels_;
    uint32_t range_;
};

}  // namespace a3
#endif  // A3_PFIFO_H_
/* vim: set sw=4 ts=4 et tw=80 : */
