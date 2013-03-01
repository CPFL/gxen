// Remapping guest physical GPU address (PRAMIN address) to host physical GPU address,
// and trapping GPU page table changes
//
// gphys -> hphys
//
#ifndef HW_NVC0_NVC0_REMAPPING_H_
#define HW_NVC0_NVC0_REMAPPING_H_
#include <stdint.h>
#include <vector>
#include "nvc0.h"
#include "nvc0_static_assert.h"
#include "nvc0_array.h"
#include "nvc0_unique_ptr.h"
namespace nvc0 {
namespace remapping {

static const uint64_t kPAGE = 0x4000;
static const uint64_t kPAGE_BITS = 12;
static const uint64_t kPAGE_DIRECTORY_BITS = 10;
static const uint64_t kPAGE_DIRECTORY_TABLE_BITS = 10;
static const uint64_t kPAGE_TABLE_BITS = 12;

// address remapping structure 10bits
class page_entry {
 public:
 private:
    uint64_t entry;
};

// point page entries 10bits
class page_directory {
 private:
    std::array<page_entry, 0x1 << kPAGE_DIRECTORY_BITS> entries_;
};

// point page directories
class page_directory_table {
 public:
 private:
    std::array<page_directory*, 0x1 << kPAGE_DIRECTORY_TABLE_BITS> directories_;
};

class table {
 public:
 private:
    std::array<page_directory_table*, 0x1 << kPAGE_TABLE_BITS> tables_;
};

} }  // namespace remapping::nvc0
#endif  // HW_NVC0_NVC0_REMAPPING_H_
/* vim: set sw=4 ts=4 et tw=80 : */
