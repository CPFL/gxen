#ifndef A3_SHADOW_PAGE_TABLE_H_
#define A3_SHADOW_PAGE_TABLE_H_
#include <cstdint>
#include <vector>
#include <memory>
#include <array>
#include <boost/ptr_container/ptr_vector.hpp>
#include <memory>
#include "page_table.h"
#include "page.h"
namespace a3 {
class context;
class page;

struct shadow_page_entry_t {
    page_directory value;
    std::shared_ptr<std::array<page_entry, kLARGE_PAGE_COUNT>> large;
    std::shared_ptr<std::array<page_entry, kSMALL_PAGE_COUNT>> small;

    void ensure_large_page_table() {
        typedef std::array<page_entry, kLARGE_PAGE_COUNT> large_t;
        if (!large) {
            large = std::shared_ptr<large_t>(new large_t());
        }
    }
    void ensure_small_page_table() {
        typedef std::array<page_entry, kSMALL_PAGE_COUNT> small_t;
        if (!small) {
            small = std::shared_ptr<small_t>(new small_t());
        }
    }
};

struct shadow_page_directory_t {
    std::array<shadow_page_entry_t, 8192> entries;
};

class shadow_page_table {
 public:
    shadow_page_table(uint32_t channel_id);
    bool refresh(context* ctx, uint64_t page_directory_address, uint64_t page_limit);
    void refresh_page_directories(context* ctx, uint64_t address);
    void temporary_replace(context* ctx, uint64_t shadow);
    void set_low_size(uint32_t value) { low_size_ = value; }
    void set_high_size(uint32_t value) { high_size_ = value; }
    uint64_t size() const { return size_; }
    uint32_t page_directory_size() const {
        //return 0x10000 / 0x8;
        return round_up(size(), kPAGE_DIRECTORY_COVERED_SIZE) / kPAGE_DIRECTORY_COVERED_SIZE;
    }
    uint32_t channel_id() const { return channel_id_; }
    uint64_t page_directory_address() const { return page_directory_address_; }
    void allocate_shadow_address();
    uint64_t shadow_address() const { return phys() ? phys()->address() : 0; }

 private:
    struct page_directory refresh_directory(context* ctx, pmem::accessor* pmem, uint32_t index);
    static uint64_t round_up(uint64_t x, uint64_t y) {
        return (((x) + (y - 1)) & ~(y - 1));
    }
    inline page* allocate_large_page();
    inline page* allocate_small_page();
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
    std::unique_ptr<page> phys_;
    boost::ptr_vector<page> large_pages_pool_;
    boost::ptr_vector<page> small_pages_pool_;
    std::size_t large_pages_pool_cursor_;
    std::size_t small_pages_pool_cursor_;
    shadow_page_directory_t directory_;
};

inline page* shadow_page_table::allocate_large_page() {
    if (large_pages_pool_cursor_ == large_pages_pool_.size()) {
        page* ptr(new page(kLARGE_PAGE_COUNT * 0x8 / kPAGE_SIZE));
        large_pages_pool_.push_back(ptr);
        return ptr;
    }
    return &large_pages_pool_[large_pages_pool_cursor_++];
}

inline page* shadow_page_table::allocate_small_page() {
    if (small_pages_pool_cursor_ == small_pages_pool_.size()) {
        page* ptr = new page(kSMALL_PAGE_COUNT * 0x8 / kPAGE_SIZE);
        small_pages_pool_.push_back(ptr);
        return ptr;
    }
    return &small_pages_pool_[small_pages_pool_cursor_++];
}

}  // namespace a3
#endif  // A3_SHADOW_PAGE_TABLE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
