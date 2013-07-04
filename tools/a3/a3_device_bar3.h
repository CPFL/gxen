#ifndef A3_DEVICE_BAR3_H_
#define A3_DEVICE_BAR3_H_
#include <boost/noncopyable.hpp>
#include "a3.h"
#include "a3_page.h"
#include "a3_device.h"
#include "a3_inttypes.h"
#include "a3_page_table.h"
#include "a3_size.h"
namespace a3 {

class context;

// TODO(Yusuke Suzuki):
// This is hard coded value 16MB / 2
// Because BAR3 effective area is limited to 16MB
static const uint64_t kBAR3_ARENA_SIZE = 8 * size::MB;
static const uint64_t kBAR3_TOTAL_SIZE = 16 * size::MB;

class device_bar3 : private boost::noncopyable {
 public:
    friend class device;

    device_bar3(device::bar_t bar);
    void refresh();
    void shadow(context* ctx, uint64_t phys);
    page* directory() { return &directory_; }

    uint64_t size() const { return size_; }
    uintptr_t address() const { return address_; }
    void flush();
    void pv_reflect(context* ctx, uint32_t index, uint64_t pte);

 private:
    void reflect_internal(bool map);
    void map(uint64_t index, const struct page_entry& pdata);
    void map_xen_page(context* ctx, uint64_t offset);
    void unmap_xen_page(context* ctx, uint64_t offset);

    uintptr_t address_;
    uint64_t size_;
    page ramin_;
    page directory_;
    page entries_;
};

}  // namespace a3
#endif  // A3_DEVICE_BAR3_H_
/* vim: set sw=4 ts=4 et tw=80 : */
