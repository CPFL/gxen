#ifndef A3_DEVICE_BAR3_H_
#define A3_DEVICE_BAR3_H_
#include <boost/noncopyable.hpp>
#include <boost/dynamic_bitset.hpp>
#include "a3.h"
#include "a3_page.h"
#include "a3_device.h"
#include "a3_inttypes.h"
namespace a3 {

class context;

class device_bar3 : private boost::noncopyable {
 public:
    friend class device;

    device_bar3(device::bar_t bar);
    void refresh();
    void shadow(context* ctx);

    uint64_t size() const { return size_; }
    uintptr_t address() const { return address_; }
    void flush();

 private:
    void map(uint64_t index, uint64_t pdata);
    void map_xen_page(context* ctx, uint64_t offset);
    void unmap_xen_page(context* ctx, uint64_t offset);

    uintptr_t address_;
    uint64_t size_;
    page ramin_;
    page directory_;
    page entries_;
    boost::dynamic_bitset<> xen_;
};

}  // namespace a3
#endif  // A3_DEVICE_BAR3_H_
/* vim: set sw=4 ts=4 et tw=80 : */
