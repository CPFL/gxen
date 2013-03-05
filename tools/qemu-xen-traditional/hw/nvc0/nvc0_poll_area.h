#ifndef HW_NVC0_NVC0_POLL_AREA_H_
#define HW_NVC0_NVC0_POLL_AREA_H_
#include "nvc0.h"
namespace nvc0 {

class context;

class poll_area {
 public:
    poll_area();
    void set_offset(context* ctx, uint64_t value);
    uint64_t offset() const { return offset_; }
    bool in_range(uint64_t offset) { return offset_ <= offset && offset < offset_ + (128 * 0x1000); }

 private:
    uint64_t offset_;
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_POLL_AREA_H_
/* vim: set sw=4 ts=4 et tw=80 : */
