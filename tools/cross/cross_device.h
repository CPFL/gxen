#ifndef CROSS_DEVICE_H_
#define CROSS_DEVICE_H_
#include <pciaccess.h>
#include <boost/thread.hpp>
#include <boost/dynamic_bitset.hpp>
#include "cross.h"
#include "cross_session.h"
#include "cross_allocator.h"
namespace cross {

class device {
 public:
    device();
    ~device();
    void initialize(const bdf& bdf);
    static device* instance();
    bool initialized() const { return device_; }
    uint32_t acquire_virt();
    void release_virt(uint32_t virt);

 private:
    struct pci_device* device_;
    boost::dynamic_bitset<> virts_;
    allocator memory_;
    boost::mutex mutex_;
};

}  // namespace cross
#endif  // CROSS_DEVICE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
