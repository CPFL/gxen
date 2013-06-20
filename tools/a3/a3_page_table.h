#ifndef A3_PAGE_TABLE_H_
#define A3_PAGE_TABLE_H_
#include <stdint.h>
#include <boost/static_assert.hpp>
#include "a3_pmem.h"
namespace a3 {

// We assume Little Endianess machine.

static const unsigned kPAGE_SHIFT = 12;
static const unsigned kSMALL_PAGE_SHIFT = 12;
static const unsigned kLARGE_PAGE_SHIFT = 17;
static const unsigned kPAGE_TABLE_BITS = 27 - 12;
static const unsigned kPAGE_SIZE = 0x1000;
static const unsigned kPAGE_TABLE_SIZE = 0x8000;

static const unsigned kPAGE_DIRECTORY_COVERED_SIZE = 0x8000000;
static const unsigned kMAX_PAGE_DIRECTORIES = 0x2000;

static const unsigned kSMALL_PAGE_SIZE = 0x1 << kSMALL_PAGE_SHIFT;
static const unsigned kLARGE_PAGE_SIZE = 0x1 << kLARGE_PAGE_SHIFT;

static const unsigned kSMALL_PAGE_COUNT = kPAGE_DIRECTORY_COVERED_SIZE >> kSMALL_PAGE_SHIFT;
static const unsigned kLARGE_PAGE_COUNT = kPAGE_DIRECTORY_COVERED_SIZE >> kLARGE_PAGE_SHIFT;

struct page_descriptor {
    union {
        uint64_t dword0;
        struct {
            uint32_t page_directory_address_low;
            uint32_t page_directory_address_high : 8;
        };
        struct {
            uint64_t page_directory_address : 40;
        };
    };
    union {
        uint64_t dword1;
        struct {
            uint32_t page_limit_low;
            uint32_t page_limit_high : 8;
        };
        struct {
            uint64_t page_limit : 40;
        };
    };
};

BOOST_STATIC_ASSERT(sizeof(struct page_descriptor) == (sizeof(uint64_t) * 2));

struct page_entry {
    enum target_type_t {
        TARGET_TYPE_VRAM            = 0,
        TARGET_TYPE_SYSRAM          = 2,
        TARGET_TYPE_SYSRAM_NO_SNOOP = 3
    };

    union {
        uint64_t raw;
        struct {
            union {
                uint32_t word0;
                struct {
                    unsigned present : 1;
                    unsigned supervisor_only : 1;
                    unsigned read_only : 1;
                    unsigned encrypted : 1;        // only used by SYSRAM
                    unsigned address: 28;
                };
            };
            union {
                uint32_t word1;
                struct {
                    unsigned unknown0 : 1;
                    unsigned target : 2;
                    unsigned unknown1 : 1;
                    unsigned storage_type : 8;      // 0 / 0xdb ZETA / 0xfe tiled surface
                    unsigned tag : 17;
                    unsigned unknown2 : 3;
                };
            };
        };
    };

    static bool create(pmem::accessor* pmem, uint64_t address, struct page_entry* entry) {
        entry->word0 = pmem->read32(address);
        if (!entry->present) {
            return false;
        }
        entry->word1 = pmem->read32(address + 0x4);
        return true;
    }
};

BOOST_STATIC_ASSERT(sizeof(struct page_entry) == sizeof(uint64_t));

struct page_directory {
    enum size_type_t {
        SIZE_TYPE_128M = 0,
        SIZE_TYPE_64M  = 1,
        SIZE_TYPE_32M  = 2,
        SIZE_TYPE_16M  = 3
    };

    union {
        uint64_t raw;
        struct {
            union {
                uint32_t word0;
                struct {
                    unsigned large_page_table_present : 1;
                    unsigned unknown0 : 1;
                    unsigned size_type : 2;
                    unsigned large_page_table_address : 28;  // 12bit shifted
                };
            };
            union {
                uint32_t word1;
                struct {
                    unsigned small_page_table_present : 1;
                    unsigned unknown1 : 1;
                    unsigned unknown2 : 1;
                    unsigned unknown3 : 1;
                    unsigned small_page_table_address : 28;  // 12bit shifted
                };
            };
        };
    };

    static struct page_directory create(pmem::accessor* pmem, uint64_t address) {
        struct page_directory dir = { { } };
        dir.word0 = pmem->read32(address);
        dir.word1 = pmem->read32(address + 0x4);
        return dir;
    }
};

BOOST_STATIC_ASSERT(sizeof(struct page_directory) == sizeof(uint64_t));

}  // namespace a3
#endif  // A3_PAGE_TABLE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
