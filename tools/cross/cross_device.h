#ifndef CROSS_DEVICE_H_
#define CROSS_DEVICE_H_
#include <pciaccess.h>
#include <boost/thread.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/noncopyable.hpp>
#include "cross.h"
#include "cross_session.h"
#include "cross_allocator.h"
namespace cross {

class device : private boost::noncopyable {
 public:
    typedef boost::recursive_mutex mutex;
    struct bar_t {
        void* addr;
        std::size_t size;
    };
    device();
    ~device();
    void initialize(const bdf& bdf);
    static device* instance();
    bool initialized() const { return device_; }
    uint32_t acquire_virt();
    void release_virt(uint32_t virt);
    mutex& mutex_handle() { return mutex_handle_; }
    uint32_t read(int bar, uint32_t offset);
    void write(int bar, uint32_t offset, uint32_t val);
    uint32_t pramin() const { return pramin_; }
    void set_pramin(uint32_t pramin) { pramin_ = pramin; }

 private:
    struct pci_device* device_;
    boost::dynamic_bitset<> virts_;
    allocator memory_;
    mutex mutex_handle_;
    uint32_t pramin_;
    boost::array<bar_t, 5> bars_;
};

}  // namespace cross
#endif  // CROSS_DEVICE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
