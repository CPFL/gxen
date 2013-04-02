#ifndef CROSS_VRAM_H_
#define CROSS_VRAM_H_
#include <cstddef>
#include <cstdlib>
#include <boost/pool/pool.hpp>
#include <boost/noncopyable.hpp>
#include "cross.h"
namespace cross {

struct gpu_memory_allocator {
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    static char* base;

    // real memory is not needed
    static char* malloc(const size_type bytes) {
        if (base) {
            CROSS_UNREACHABLE();
        }
        base = reinterpret_cast<char*>(std::malloc(bytes));
        return base;
    }

    static void free(char* const block) {
        std::free(block);
        base = NULL;
    }
};

class vram;

class vram_memory : private boost::noncopyable {
 public:
    friend class vram;
    uint64_t address() const { return address_; }
    std::size_t n() const { return n_; }

 private:
    vram_memory(uint64_t address, std::size_t n) : address_(address), n_(n) { }

    uint64_t address_;
    std::size_t n_;    // page size
};

class vram {
 public:
    vram(uint64_t mem, uint64_t size);
    vram_memory* malloc(std::size_t n = 1);  // n is the number of pages
    void free(vram_memory* mem);

 private:
    uint64_t encode(void* ptr);
    void* decode(uint64_t address);

    uint64_t mem_;
    uint64_t size_;
    boost::pool<gpu_memory_allocator> pool_;
};

}  // namespace cross
#endif  // CROSS_VRAM_H_
/* vim: set sw=4 ts=4 et tw=80 : */
