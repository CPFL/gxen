#ifndef A3_DEVICE_H_
#define A3_DEVICE_H_
#include <vector>
#include <array>
#include <memory>
#include <pciaccess.h>
#include <boost/dynamic_bitset.hpp>
#include <boost/noncopyable.hpp>
#include "a3.h"
#include "xen.h"
#include "lock.h"
#include "session.h"
#include "chipset.h"
namespace a3 {

class device_bar1;
class device_bar3;
class vram_manager_t;
class vram_t;
class context;
class playlist_t;
class scheduler_t;

class device_t : private boost::noncopyable {
 public:
    struct bar_t {
        void* addr;
        uintptr_t base_addr;
        std::size_t size;
    };

    friend class device_bar1;
    friend class device_bar3;

    device_t();
    ~device_t();
    void initialize(const bdf& bdf);
    static device_t* instance();
    bool initialized() const { return device_; }
    uint32_t acquire_virt(context* ctx);
    void release_virt(uint32_t virt, context* ctx);
    mutex_t& mutex() { return mutex_; }
    uint32_t read(int bar, uint32_t offset, std::size_t size);
    void write(int bar, uint32_t offset, uint32_t val, std::size_t size);
    uint32_t read_pmem(uint64_t addr, std::size_t size);
    void write_pmem(uint64_t addr, uint32_t val, std::size_t size);
    uint32_t pmem() const { return pmem_; }
    void set_pmem(uint32_t pmem) { pmem_ = pmem; }
    device_bar1* bar1() { return bar1_.get(); }
    const device_bar1* bar1() const { return bar1_.get(); }
    device_bar3* bar3() { return bar3_.get(); }
    const device_bar3* bar3() const { return bar3_.get(); }
    vram_t* malloc(std::size_t n);
    void free(vram_t* mem);
    const std::vector<context*>& contexts() const { return contexts_; }
    const chipset_t* chipset() const { return chipset_.get(); }

    // VT-d
    int domid() const { return domid_; }
    bool is_active(context* ctx);
    void fire(context* ctx, const command& cmd);

    void playlist_update(context* ctx, uint32_t address, uint32_t cmd);

    libxl_ctx* xl_ctx() const { return xl_ctx_; }

 private:
    struct pci_device* device_;
    boost::dynamic_bitset<> virts_;
    std::vector<context*> contexts_;
    mutex_t mutex_;
    uint32_t pmem_;
    std::array<bar_t, 5> bars_;
    std::unique_ptr<device_bar1> bar1_;
    std::unique_ptr<device_bar3> bar3_;
    std::unique_ptr<vram_manager_t> vram_;
    std::unique_ptr<playlist_t> playlist_;
    std::unique_ptr<scheduler_t> scheduler_;
    std::unique_ptr<chipset_t> chipset_;
    int domid_;

    // libxl
    libxl_ctx* xl_ctx_;
    xentoollog_logger_stdiostream* xl_logger_;
    libxl_device_pci xl_device_pci_;
};

device_t* device();

}  // namespace a3
#endif  // A3_DEVICE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
