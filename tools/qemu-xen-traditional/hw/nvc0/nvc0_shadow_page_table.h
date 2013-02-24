#ifndef HW_NVC0_NVC0_SHADOW_PAGE_TABLE_H_
#define HW_NVC0_NVC0_SHADOW_PAGE_TABLE_H_
#include <stdint.h>
#include "nvc0_static_assert.h"
namespace nvc0 {

// We assume Little Endianess

struct page_directory {
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

struct page_entry {
    union {
        struct {
            unsigned page_present : 1;
            unsigned supervisor_page : 1;
            unsigned read_only : 1;
            unsigned unknown1 : 1;
            unsigned page_address : 28;
        };
        uint32_t word0;
    };
    union {
        struct {
            unsigned unknown2 : 1;
            unsigned select_target : 2;
            unsigned unknown3 : 1;
            unsigned memory_type : 8;  // 0 / 0xdb ZETA / 0xfe tiled surface
            unsigned unknown4 : 20;
        };
        uint32_t word1;
    };
};

NVC0_STATIC_ASSERT(sizeof(struct page_entry) == sizeof(uint64_t), page_entry_size_is_invalid);

class shadow_page_table {
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_SHADOW_PAGE_TABLE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
