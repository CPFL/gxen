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
class device_bar3;
class vram;
class vram_memory;
class context;
class playlist_t;

class device : private boost::noncopyable {
 public:
    struct bar_t {
        void* addr;
        uintptr_t base_addr;
        std::size_t size;
    };

    friend class device_bar1;
    friend class device_bar3;

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
    uint32_t pmem() const { return pmem_; }
    void set_pmem(uint32_t pmem) { pmem_ = pmem; }
    device_bar1* bar1() { return bar1_.get(); }
    const device_bar1* bar1() const { return bar1_.get(); }
    device_bar3* bar3() { return bar3_.get(); }
    const device_bar3* bar3() const { return bar3_.get(); }
    vram_memory* malloc(std::size_t n);
    void free(vram_memory* mem);

    // VT-d
    bool try_acquire_gpu(context* ctx);
    void acquire_gpu(context* ctx);
    int domid() const { return domid_; }
    bool is_active();
    void fire(context* ctx, const command& cmd);

    void playlist_update(context* ctx, uint32_t address, uint32_t cmd);

 private:
    libxl_ctx* xl_ctx() const { return xl_ctx_; }

    struct pci_device* device_;
    boost::dynamic_bitset<> virts_;
    mutex mutex_handle_;
    uint32_t pmem_;
    boost::array<bar_t, 5> bars_;
    boost::scoped_ptr<device_bar1> bar1_;
    boost::scoped_ptr<device_bar3> bar3_;
    boost::scoped_ptr<vram> vram_;
    boost::scoped_ptr<playlist_t> playlist_;
    timer_t timer_;
    int domid_;

    // libxl
    libxl_ctx* xl_ctx_;
    xentoollog_logger_stdiostream* xl_logger_;
    libxl_device_pci xl_device_pci_;
};

}  // namespace a3
#endif  // A3_DEVICE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
