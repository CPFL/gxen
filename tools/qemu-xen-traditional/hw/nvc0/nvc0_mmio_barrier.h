#ifndef HW_NVC0_NVC0_MMIO_BARRIER_H_
#define HW_NVC0_NVC0_MMIO_BARRIER_H_
#include <map>
#include <stdint.h>
#include "nvc0.h"
#include "nvc0_noncopyable.h"
namespace nvc0 {

class mmio_barrier {
 public:
    typedef std::pair<uint64_t, uint64_t> interval;  // start address and end address
    typedef std::map<interval, bool> barrier_map;

 private:
    barrier_map map_;
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_MMIO_BARRIER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
