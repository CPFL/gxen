#ifndef HW_NVC0_NVC0_TLB_FLUSH_H_
#define HW_NVC0_NVC0_TLB_FLUSH_H_
#include "nvc0.h"
namespace nvc0 {

class context;

class tlb_flush {
 public:
    void set_vspace(uint32_t vspace) { vspace_ = vspace; }
    uint32_t vspace() const { return vspace_; }
    uint32_t trigger() const { return trigger_; }
    void trigger(context* ctx, uint32_t val);
 private:
    uint32_t vspace_;
    uint32_t trigger_;
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_TLB_FLUSH_H_
/* vim: set sw=4 ts=4 et tw=80 : */
