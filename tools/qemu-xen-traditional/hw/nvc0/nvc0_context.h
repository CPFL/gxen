#ifndef HW_NVC0_NVC0_CONTEXT_H_
#define HW_NVC0_NVC0_CONTEXT_H_
#include "nvc0.h"
#include "nvc0_shadow_page_table.h"
namespace nvc0 {

class context {
 public:
    context(nvc0_state_t* state);
    shadow_page_table* bar1_table() { return &bar1_table_; }
    shadow_page_table* bar3_table() { return &bar3_table_; }

    static context* extract(nvc0_state_t* state);

 private:
    nvc0_state_t* state_;
    shadow_page_table bar1_table_;
    shadow_page_table bar3_table_;
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_CONTEXT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
