#ifndef A3_DEVICE_H_
#define A3_DEVICE_H_
#include <vector>
#include <pciaccess.h>
#include <boost/dynamic_bitset.hpp>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>
#include "a3.h"
#include "a3_xen.h"
#include "a3_lock.h"
#include "a3_session.h"
#include "a3_timer.h"
namespace a3 {

class device_bar1;
class vram;
class vram_memory;
class context;

class device : private boost::noncopyable {
 public:
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
    uint32_t read(int bar, uint32_t offset, std::size_t size);
    void write(int bar, uint32_t offset, uint32_t val, std::size_t size);
    uint32_t pramin() const { return pramin_; }
    void set_pramin(uint32_t pramin) { pramin_ = pramin; }
    device_bar1* bar1() { return bar1_.get(); }
    const device_bar1* bar1() const { return bar1_.get(); }
    vram_memory* malloc(std::size_t n);
    void free(vram_memory* mem);

    // VT-d
    bool try_acquire_gpu(context* ctx);
    void acquire_gpu(context* ctx);
    int domid() const { return domid_; }
    bool is_active();

 private:
    struct pci_device* device_;
    boost::dynamic_bitset<> virts_;
    mutex mutex_handle_;
    uint32_t pramin_;
    boost::array<bar_t, 5> bars_;
    boost::scoped_ptr<device_bar1> bar1_;
    boost::scoped_ptr<vram> vram_;
    // libxl
    libxl_ctx* xl_ctx_;
    xentoollog_logger_stdiostream* xl_logger_;
    libxl_device_pci xl_device_pci_;
    int domid_;
    timer_t timer_;
};

}  // namespace a3
#endif  // A3_DEVICE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
