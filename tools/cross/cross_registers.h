#ifndef CROSS_REGISTERS_H_
#define CROSS_REGISTERS_H_
#include <boost/noncopyable.hpp>
#include "cross_device.h"
namespace cross {
namespace registers {

class accessor : private boost::noncopyable {
 public:
    explicit accessor();
    uint32_t read32(uint32_t offset);
    void write32(uint32_t offset, uint32_t val);
    bool wait_eq(uint32_t offset, uint32_t mask, uint32_t val);
    bool wait_ne(uint32_t offset, uint32_t mask, uint32_t val);

    template<typename Functor>
    bool wait_cb(uint32_t offset, uint32_t mask, uint32_t val, const Functor& func) {
        uint64_t counter = 0;
        do {
            if (func((read32(offset) & mask), val)) {
                return true;
            }
            ++counter;
            if (counter % 100000) {
                CROSS_LOG("wait stop count %" PRIX64 "\n", counter);
            }
        } while (true);
        return false;
    }

 private:
    mutex::scoped_lock lock_;
};

inline uint32_t read32(uint32_t offset) {
    accessor regs;
    return regs.read32(offset);
}

inline void write32(uint32_t offset, uint32_t val) {
    accessor regs;
    regs.write32(offset, val);
}

inline bool wait_eq(uint32_t offset, uint32_t mask, uint32_t val) {
    accessor regs;
    return regs.wait_eq(offset, mask, val);
}

inline bool wait_ne(uint32_t offset, uint32_t mask, uint32_t val) {
    accessor regs;
    return regs.wait_ne(offset, mask, val);
}

} }  // namespace cross::registers
#endif  // CROSS_REGISTERS_H_
/* vim: set sw=4 ts=4 et tw=80 : */
