#ifndef HW_NVC0_NVC0_MMIO_BARRIER_H_
#define HW_NVC0_NVC0_MMIO_BARRIER_H_
#include <map>
#include <stdint.h>
#include "nvc0.h"
#include "nvc0_unordered_map.h"
#include "nvc0_noncopyable.h"
namespace nvc0 {

class mmio_barrier {
 public:
    typedef std::pair<uint64_t, uint64_t> interval;  // [start, end)
    typedef std::map<interval, uint32_t> barrier_map;
    typedef std::unordered_multimap<uint32_t, interval> item_map;

    bool handle(uint64_t address);
    void clear(uint32_t type);
    void register_barrier(uint32_t type, interval i);

 private:
    barrier_map barriers_;
    item_map items_;
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_MMIO_BARRIER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
