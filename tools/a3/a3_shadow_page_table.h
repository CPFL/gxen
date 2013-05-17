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

class shadow_page_entry {
 public:
    struct page_entry refresh(context* ctx, pramin::accessor* pramin, const struct page_entry& entry);
    const struct page_entry& virt() const { return virt_; }
    const struct page_entry& phys() const { return phys_; }
    bool present() const { return virt_.present; }

 private:
    struct page_entry virt_;
    struct page_entry phys_;
};

class shadow_page_directory {
 public:
    typedef std::vector<shadow_page_entry> shadow_page_entries;

    struct page_directory refresh(context* ctx, pramin::accessor* pramin, const struct page_directory& dir);
    const struct page_directory& virt() const { return virt_; }
    const struct page_directory& phys() const { return phys_; }
    uint64_t resolve(uint64_t offset, struct shadow_page_entry* result);
    const shadow_page_entries& large_entries() const { return large_entries_; }
    const shadow_page_entries& small_entries() const { return small_entries_; }

 private:
    page* large_page() { return large_page_.get(); }
    const page* large_page() const { return large_page_.get(); }
    page* small_page() { return small_page_.get(); }
    const page* small_page() const { return small_page_.get(); }

    struct page_directory virt_;
    struct page_directory phys_;
    shadow_page_entries large_entries_;
    shadow_page_entries small_entries_;
    boost::shared_ptr<page> large_page_;
    boost::shared_ptr<page> small_page_;
};

class shadow_page_table {
 public:
    typedef std::vector<shadow_page_directory> shadow_page_directories;

    shadow_page_table(uint32_t channel_id);
    bool refresh(context* ctx, uint64_t page_directory_address, uint64_t page_limit);
    void refresh_page_directories(context* ctx, uint64_t address);
    void set_low_size(uint32_t value);
    void set_high_size(uint32_t value);
    uint64_t size() const { return size_; }
    uint32_t page_directory_size() const { return round_up(size(), kPAGE_DIRECTORY_COVERED_SIZE) / kPAGE_DIRECTORY_COVERED_SIZE; }
    uint32_t channel_id() const { return channel_id_; }
    uint64_t resolve(uint64_t virtual_address, struct shadow_page_entry* result);
    uint64_t page_directory_address() const { return page_directory_address_; }
    void dump() const;
    uint64_t shadow_address() const { return phys() ? phys()->address() : 0; }

 private:
    static uint64_t round_up(uint64_t x, uint64_t y) {
        return (((x) + (y - 1)) & ~(y - 1));
    }
    page* phys() { return phys_.get(); };
    const page* phys() const { return phys_.get(); };

    shadow_page_directories directories_;
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
};

}  // namespace a3
#endif  // A3_SHADOW_PAGE_TABLE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
