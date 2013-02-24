#ifndef HW_NVC0_NVC0_SHADOW_PAGE_TABLE_H_
#define HW_NVC0_NVC0_SHADOW_PAGE_TABLE_H_
#include <stdint.h>
#include <vector>
#include "nvc0_static_assert.h"
namespace nvc0 {

// We assume Little Endianess machine.

static const unsigned kSMALL_PAGE_SHIFT = 12;
static const unsigned kLARGE_PAGE_SHIFT = 17;
static const unsigned kPAGE_TABLE_BITS = 27;
static const unsigned kBLOCK = 4096;
static const unsigned kPAGE_TABLE_SIZE = 0x8000;

struct page_directory {
    enum size_type_t {
        SIZE_TYPE_FULL = 0,
        SIZE_TYPE_64M  = 1,
        SIZE_TYPE_32M  = 2,
        SIZE_TYPE_16M  = 3,
    };

    union {
        struct {
            unsigned large_page_table_present : 1;
            unsigned unknown1 : 1;
            unsigned size_type : 2;
            unsigned large_page_table_address : 28;  // 12bit shifted
        };
        uint32_t word0;
    };
    union {
        struct {
            unsigned small_page_table_present : 1;
            unsigned unknown2 : 1;
            unsigned unknown3 : 1;
            unsigned unknown4 : 1;
            unsigned small_page_table_address : 28;  // 12bit shifted
        };
        uint32_t word1;
    };
};

NVC0_STATIC_ASSERT(sizeof(struct page_directory) == sizeof(uint64_t), page_directory_size_is_invalid);

class shadow_page_directory {
 private:
    struct page_directory virt_;
    struct page_directory phys_;
};

struct page_entry {
    union {
        struct {
            unsigned present : 1;
            unsigned supervisor_only : 1;
            unsigned read_only : 1;
            unsigned encrypted : 1;        // only used by SYSRAM
            unsigned address: 28;
        };
        uint32_t word0;
    };
    union {
        struct {
            unsigned unknown1 : 1;
            unsigned target : 2;
            unsigned unknown2 : 1;
            unsigned storage_type : 8;      // 0 / 0xdb ZETA / 0xfe tiled surface
            unsigned tag : 17;
            unsigned unknown3 : 3;
        };
        uint32_t word1;
    };
};

NVC0_STATIC_ASSERT(sizeof(struct page_entry) == sizeof(uint64_t), page_entry_size_is_invalid);

class shadow_page_entry {
 private:
    struct page_entry virt_;
    struct page_entry phys_;
};

class shadow_page_table {
 public:
    void refresh(uint64_t value);
    void set_low_size(uint32_t value);
    void set_high_size(uint32_t value);
    uint64_t size() const { return size_; }

 private:
    std::vector<shadow_page_directory> directories_;
    union {
        struct {
            uint32_t low_size_ : 32;
            uint32_t high_size_ : 8;
        };
        uint64_t size_ : 40;
    };
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_SHADOW_PAGE_TABLE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
