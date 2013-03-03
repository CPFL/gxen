#ifndef HW_NVC0_NVC0_CONTEXT_H_
#define HW_NVC0_NVC0_CONTEXT_H_
#include "nvc0.h"
#include "nvc0_shadow_page_table.h"
#include "nvc0_mmio_barrier.h"
#include "nvc0_remapping.h"
#include "nvc0_tlb_flush.h"
namespace nvc0 {

class context {
 public:
    explicit context(nvc0_state_t* state, uint64_t memory_size);
    nvc0_state_t* state() const { return state_; }
    shadow_page_table* bar1_table() { return &bar1_table_; }
    shadow_page_table* bar3_table() { return &bar3_table_; }
    mmio_barrier* barrier() { return &barrier_; }
    nvc0::remapping::table* remapping() { return &remapping_; }
    tlb_flush* tlb() { return &tlb_; }
    uint64_t pramin() const { return pramin_; }
    void set_pramin(uint64_t pramin) { pramin_ = pramin; }

    static context* extract(nvc0_state_t* state);

 private:
    nvc0_state_t* state_;
    shadow_page_table bar1_table_;
    shadow_page_table bar3_table_;
    mmio_barrier barrier_;
    nvc0::remapping::table remapping_;
    tlb_flush tlb_;
    uint64_t pramin_;  // 16bit shifted
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_CONTEXT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
