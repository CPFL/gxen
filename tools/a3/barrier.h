#ifndef A3_BARRIER_H_
#define A3_BARRIER_H_
#include <cstdint>
#include <cstdio>
#include <vector>
#include <array>
#include <boost/shared_ptr.hpp>
#include <boost/static_assert.hpp>
namespace a3 {
namespace barrier {

static const uint64_t kPAGE = 0x4000;
static const uint64_t kPAGE_BITS           = 12;
static const uint64_t kPAGE_ENTRY_BITS     = 15;
static const uint64_t kPAGE_DIRECTORY_BITS = 13;
static const uint64_t kADDRESS_BITS = kPAGE_BITS + kPAGE_ENTRY_BITS + kPAGE_DIRECTORY_BITS;  // 40bits

struct address_t {
    union {
        uint64_t raw;
        struct {
            uint64_t offset    : 12;
            uint64_t page      : 15;
            uint64_t directory : 13;
            uint64_t unused    : 24;
        };
    };
};

BOOST_STATIC_ASSERT(sizeof(address_t) == sizeof(uint64_t));

class page_entry {
 public:
    page_entry() : ref_count_(0) { }
    bool present() const { return ref_count_ != 0; }
    void release() { --ref_count_; }
    void retain() { ++ref_count_; }
 private:
    uint8_t ref_count_;
};

// point page entries 10bits
class page_directory {
 public:
    bool map(uint64_t page_start_address);
    void unmap(uint64_t page_start_address);
    bool lookup(uint64_t address, page_entry** entry);

 private:
    std::array<page_entry, 0x1 << kPAGE_ENTRY_BITS> entries_;
};

class table {
 public:
    typedef boost::shared_ptr<page_directory> directory;
    table(uint64_t base, uint64_t size);
    // returns previous definition exists
    bool map(uint64_t page_start_address);
    bool unmap(uint64_t page_start_address);
    bool lookup(uint64_t address, page_entry** entry, bool force_create = true);
    uint64_t base() const { return base_; }
    uint64_t size() const { return size_; }

 private:
    bool in_range(uint64_t address) const;

    std::vector<directory> table_;
    uint64_t base_;
    uint64_t size_;
};

} }  // namespace a3::barrier
#endif  // A3_BARRIER_H_
/* vim: set sw=4 ts=4 et tw=80 : */
