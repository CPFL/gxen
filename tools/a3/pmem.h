#ifndef A3_pmem_H_
#define A3_pmem_H_
#include <boost/noncopyable.hpp>
#include "device.h"
namespace a3 {
namespace pmem {

class accessor : private boost::noncopyable {
 public:
    accessor()
        : lock_(a3::device()->mutex())
    {
    }

    uint32_t read(uint64_t addr, std::size_t size);
    void write(uint64_t addr, uint32_t val, std::size_t size);

    uint32_t read32(uint64_t addr) {
        return read(addr, sizeof(uint32_t));
    }

    void write32(uint64_t addr, uint32_t val) {
        write(addr, val, sizeof(uint32_t));
    }

    uint32_t read16(uint64_t addr) {
        return read(addr, sizeof(uint16_t));
    }

    void write16(uint64_t addr, uint16_t val) {
        write(addr, val, sizeof(uint16_t));
    }

    uint32_t read8(uint64_t addr) {
        return read(addr, sizeof(uint8_t));
    }

    void write8(uint64_t addr, uint8_t val) {
        write(addr, val, sizeof(uint8_t));
    }

 private:
    mutex_t::scoped_lock lock_;
};

inline uint32_t read32(uint64_t addr) {
    accessor pmem;
    return pmem.read32(addr);
}

inline void write32(uint64_t addr, uint32_t val) {
    accessor pmem;
    pmem.write32(addr, val);
}

} }  // namespace a3::pmem
#endif  // A3_pmem_H_
/* vim: set sw=4 ts=4 et tw=80 : */
