#ifndef A3_SHADOW_PAGE_TABLE_H_
#define A3_SHADOW_PAGE_TABLE_H_
#include <stdint.h>
#include <vector>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include "a3_page_table.h"
#include "a3_page.h"
namespace a3 {
class context;
class page;

class shadow_page_table {
 public:
    shadow_page_table(uint32_t channel_id);
    bool refresh(context* ctx, uint64_t page_directory_address, uint64_t page_limit);
    void refresh_page_directories(context* ctx, uint64_t address);
    void set_low_size(uint32_t value);
    void set_high_size(uint32_t value);
    uint64_t size() const { return size_; }
    uint32_t page_directory_size() const {
        //return 0x10000 / 0x8;
        return round_up(size(), kPAGE_DIRECTORY_COVERED_SIZE) / kPAGE_DIRECTORY_COVERED_SIZE;
    }
    uint32_t channel_id() const { return channel_id_; }
    uint64_t page_directory_address() const { return page_directory_address_; }
    uint64_t shadow_address() const { return phys() ? phys()->address() : 0; }

 private:
    struct page_directory refresh_directory(context* ctx, pramin::accessor* pramin, const struct page_directory& dir);
    struct page_entry refresh_entry(context* ctx, pramin::accessor* pramin, const struct page_entry& entry);
    static uint64_t round_up(uint64_t x, uint64_t y) {
        return (((x) + (y - 1)) & ~(y - 1));
    }
    boost::shared_ptr<page> allocate_large_page();
    boost::shared_ptr<page> allocate_small_page();
    page* phys() { return phys_.get(); };
    const page* phys() const { return phys_.get(); };

    union {
        struct {
            uint32_t low_size_ : 32;
            uint32_t high_size_ : 8;
        };
        uint64_t size_ : 40;
    };
    uint64_t page_directory_address_;
    uint32_t channel_id_;
    boost::scoped_ptr<page> phys_;
    std::vector<boost::shared_ptr<page> > large_pages_pool_;
    std::vector<boost::shared_ptr<page> > small_pages_pool_;
    std::size_t large_pages_pool_cursor_;
    std::size_t small_pages_pool_cursor_;
};

}  // namespace a3
#endif  // A3_SHADOW_PAGE_TABLE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
