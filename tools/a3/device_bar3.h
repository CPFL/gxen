#ifndef A3_DEVICE_BAR3_H_
#define A3_DEVICE_BAR3_H_
#include <cinttypes>
#include <vector>
#include <array>
#include <boost/noncopyable.hpp>
#include "a3.h"
#include "page.h"
#include "device.h"
#include "page_table.h"
#include "size.h"
#include "software_page_table.h"
namespace a3 {

class context;

class device_bar3 : private boost::noncopyable {
 public:
    friend class device;

    device_bar3(device_t::bar_t bar);
    void refresh();
    void refresh_table(context* ctx, uint64_t phys);
    void shadow(context* ctx, uint64_t phys);
    void reset_barrier(context* ctx, uint64_t old, uint64_t addr, bool old_remap);
    page* directory() { return &directory_; }

    uint64_t size() const { return size_; }
    uintptr_t address() const { return address_; }
    void flush();
    void pv_reflect(context* ctx, uint32_t index, uint64_t guest, uint64_t host);
    void pv_reflect_batch(context* ctx, uint32_t index, uint64_t guest, uint64_t next, uint32_t count);
    void map_xen_page(context* ctx, uint64_t offset);
    void unmap_xen_page(context* ctx, uint64_t offset);
    void map_xen_page_batch(context* ctx, uint64_t offset, uint32_t count);
    void unmap_xen_page_batch(context* ctx, uint64_t offset, uint32_t count);

    uint64_t resolve(context* ctx, uint64_t virtual_address, struct software_page_entry* result);

 private:
    void reflect_internal(bool map);
    void map(uint64_t index, const struct page_entry& pdata);

    uintptr_t address_;
    uint64_t size_;
    page ramin_;
    page directory_;
    page entries_;
    std::vector<uint64_t> software_;
    std::array<software_page_entry, A3_BAR3_TOTAL_SIZE / kLARGE_PAGE_SIZE> large_;
    std::array<software_page_entry, A3_BAR3_TOTAL_SIZE / kSMALL_PAGE_SIZE> small_;
};

}  // namespace a3
#endif  // A3_DEVICE_BAR3_H_
/* vim: set sw=4 ts=4 et tw=80 : */
