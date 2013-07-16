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
namespace a3 {

class device_bar1;
class device_bar3;
class vram;
class vram_memory;
class context;
class playlist_t;
class fifo_scheduler;
class band_scheduler;

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
    uint32_t acquire_virt(context* ctx);
    void release_virt(uint32_t virt, context* ctx);
    mutex_t& mutex() { return mutex_; }
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
    int domid() const { return domid_; }
    bool is_active(context* ctx);
    void fire(context* ctx, const command& cmd);

    void playlist_update(context* ctx, uint32_t address, uint32_t cmd);

    libxl_ctx* xl_ctx() const { return xl_ctx_; }

 private:
    struct pci_device* device_;
    boost::dynamic_bitset<> virts_;
    mutex_t mutex_;
    uint32_t pmem_;
    boost::array<bar_t, 5> bars_;
    boost::scoped_ptr<device_bar1> bar1_;
    boost::scoped_ptr<device_bar3> bar3_;
    boost::scoped_ptr<vram> vram_;
    boost::scoped_ptr<playlist_t> playlist_;
    // boost::scoped_ptr<fifo_scheduler> scheduler_;
    boost::scoped_ptr<band_scheduler> scheduler_;
    int domid_;

    // libxl
    libxl_ctx* xl_ctx_;
    xentoollog_logger_stdiostream* xl_logger_;
    libxl_device_pci xl_device_pci_;
};

}  // namespace a3
#endif  // A3_DEVICE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
