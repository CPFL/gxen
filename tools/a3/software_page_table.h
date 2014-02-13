#ifndef A3_SOFTWARE_PAGE_TABLE_H_
#define A3_SOFTWARE_PAGE_TABLE_H_
#include <cstdint>
#include <vector>
#include <memory>
#include <boost/shared_ptr.hpp>
#include "page_table.h"
namespace a3 {
class context;
class page;
class pv_page;
class software_page_table;

class software_page_entry {
 public:
    friend class software_page_table;
    const struct page_entry& phys() const { return phys_; }
    bool present() const { return phys_.present; }
    void refresh(context* ctx, const struct page_entry& entry);
    inline void assign(const struct page_entry& entry);
    inline void clear() {
        struct page_entry result = {};
        phys_ = result;
    }

 private:
    struct page_entry phys_;
};

class software_page_table {
 private:
    class software_page_directory {
     public:
        typedef std::vector<software_page_entry> software_page_entries;

        void refresh(context* ctx, pmem::accessor* pmem, const struct page_directory& dir, std::size_t remain);
        uint64_t resolve(uint64_t offset, struct software_page_entry* result);
        const software_page_entries* large_entries() const { return large_entries_.get(); }
        const software_page_entries* small_entries() const { return small_entries_.get(); }
        void pv_reflect(context* ctx, bool big, uint32_t index, uint64_t entry);
        void pv_scan(context* ctx, bool big, pv_page* pgt, std::size_t remain);

     private:
        boost::shared_ptr<software_page_entries> large_entries_;
        boost::shared_ptr<software_page_entries> small_entries_;
    };
    typedef std::vector<software_page_directory> software_page_directories;

 public:
    software_page_table(uint32_t channel_id, bool para, uint64_t predefined_max = 0);
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
    uint64_t resolve(uint64_t virtual_address, struct software_page_entry* result);
    uint64_t page_directory_address() const { return page_directory_address_; }
    void dump() const;

    void pv_reflect_entry(context* ctx, uint32_t dir, bool big, uint32_t index, uint64_t entry);
    void pv_scan(context* ctx, uint32_t dir, bool big, pv_page* pgt);

 private:
    static uint64_t round_up(uint64_t x, uint64_t y) {
        return (((x) + (y - 1)) & ~(y - 1));
    }

    software_page_directories directories_;
    union {
        struct {
            uint32_t low_size_ : 32;
            uint32_t high_size_ : 8;
        };
        uint64_t size_ : 40;
    };
    uint64_t page_directory_address_;
    uint32_t channel_id_;
    uint64_t predefined_max_;
};

inline void software_page_entry::assign(const struct page_entry& entry) {
    phys_ = entry;
}

}  // namespace a3
#endif  // A3_SOFTWARE_PAGE_TABLE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
