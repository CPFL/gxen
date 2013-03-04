#ifndef HW_NVC0_NVC0_SHADOW_PAGE_TABLE_H_
#define HW_NVC0_NVC0_SHADOW_PAGE_TABLE_H_
#include <stdint.h>
#include <vector>
#include "nvc0_static_assert.h"
#include "nvc0.h"
namespace nvc0 {

class pramin_accessor;
class context;

// We assume Little Endianess machine.

static const unsigned kSMALL_PAGE_SHIFT = 12;
static const unsigned kLARGE_PAGE_SHIFT = 17;
static const unsigned kPAGE_TABLE_BITS = 27 - 12;
static const unsigned kBLOCK = 4096;
static const unsigned kPAGE_TABLE_SIZE = 0x8000;

static const unsigned kPAGE_DIRECTORY_COVERED_SIZE = 0x8000000;
static const unsigned kMAX_PAGE_DIRECTORIES = 0x2000;

static const unsigned kSMALL_PAGE_SIZE = 0x1 << kSMALL_PAGE_SHIFT;
static const unsigned kLARGE_PAGE_SIZE = 0x1 << kLARGE_PAGE_SHIFT;

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

NVC0_STATIC_ASSERT(sizeof(struct page_descriptor) == (sizeof(uint64_t) * 2), page_descriptor_size_is_invalid);

struct page_entry {
    enum target_type_t {
        TARGET_TYPE_VRAM            = 0,
        TARGET_TYPE_SYSRAM          = 2,
        TARGET_TYPE_SYSRAM_NO_SNOOP = 3
    };

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

NVC0_STATIC_ASSERT(sizeof(struct page_entry) == sizeof(uint64_t), page_entry_size_is_invalid);

class shadow_page_entry {
 public:
    bool refresh(pramin_accessor* pramin, uint64_t page_entry_address);
    const struct page_entry& virt() const { return virt_; }
    const struct page_entry& phys() const { return phys_; }
    bool present() const { return virt_.present; }

 private:
    struct page_entry virt_;
    struct page_entry phys_;
};

struct page_directory {
    enum size_type_t {
        SIZE_TYPE_FULL = 0,
        SIZE_TYPE_64M  = 1,
        SIZE_TYPE_32M  = 2,
        SIZE_TYPE_16M  = 3
    };

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

NVC0_STATIC_ASSERT(sizeof(struct page_directory) == sizeof(uint64_t), page_directory_size_is_invalid);

class shadow_page_directory {
 public:
    typedef std::vector<shadow_page_entry> shadow_page_entries;

    void refresh(context* ctx, uint32_t channel_id, pramin_accessor* pramin, uint64_t page_directory_address);
    const struct page_directory& virt() const { return virt_; }
    const struct page_directory& phys() const { return phys_; }
    uint64_t resolve(uint64_t offset);
    const shadow_page_entries& large_entries() const { return large_entries_; }
    const shadow_page_entries& small_entries() const { return small_entries_; }

 private:
    struct page_directory virt_;
    struct page_directory phys_;
    shadow_page_entries large_entries_;
    shadow_page_entries small_entries_;
};

class shadow_page_table;

class shadow_page_table_barrier {
 public:
    enum event_t {
        TABLE_CHANGE = 0,
        DIRECTORY_CHANGE = 1,
        ENTRY_CHANGE = 2
    };

 private:
    shadow_page_table* table_;
};

class shadow_page_table {
 public:
    typedef std::vector<shadow_page_directory> shadow_page_directories;

    shadow_page_table(uint32_t channel_id);
    bool refresh(context* ctx, uint32_t value);
    bool refresh_page_directories(context* ctx, uint64_t address);
    void set_low_size(uint32_t value);
    void set_high_size(uint32_t value);
    uint64_t size() const { return size_; }
    uint32_t page_directory_size() const { return round_up(size(), kPAGE_DIRECTORY_COVERED_SIZE) / kPAGE_DIRECTORY_COVERED_SIZE; }
    uint32_t channel_id() const { return channel_id_; }
    uint64_t resolve(uint64_t virtual_address);
    uint64_t page_directory_address() const { return page_directory_address_; }
    void dump() const;

 private:
    static uint64_t round_up(uint64_t x, uint64_t y) {
        return (((x) + (y - 1)) & ~(y - 1));
    }

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
};

}  // namespace nvc0
#endif  // HW_NVC0_NVC0_SHADOW_PAGE_TABLE_H_
/* vim: set sw=4 ts=4 et tw=80 : */
