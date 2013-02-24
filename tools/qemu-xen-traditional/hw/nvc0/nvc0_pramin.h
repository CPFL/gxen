#ifndef HW_NVC0_NVC0_PRAMIN_H_
#define HW_NVC0_NVC0_PRAMIN_H_
#include "nvc0.h"
#include "nvc0_noncopyable.h"
namespace nvc0 {

uint32_t pramin_read32(nvc0_state_t* state, uint64_t addr);
void pramin_write32(nvc0_state_t* state, uint64_t addr, uint32_t val);

class pramin_accessor : private noncopyable<> {
 public:
    explicit pramin_accessor(nvc0_state_t* state);
    ~pramin_accessor();

    uint32_t read32(uint64_t addr);
    void write32(uint64_t addr, uint32_t val);

 private:
    void change_current(uint64_t addr);

    nvc0_state_t* state_;
    uint32_t old_;
    uint32_t current_;
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_PRAMIN_H_
/* vim: set sw=4 ts=4 et tw=80 : */
