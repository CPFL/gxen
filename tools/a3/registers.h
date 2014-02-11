#ifndef A3_REGISTERS_H_
#define A3_REGISTERS_H_
#include <boost/noncopyable.hpp>
#include <backward.hpp>
#include "device.h"
namespace a3 {
namespace registers {

class accessor : private boost::noncopyable {
 public:
    explicit accessor();
    uint32_t read(uint32_t offset, std::size_t size);
    void write(uint32_t offset, uint32_t val, std::size_t size);
    uint32_t read32(uint32_t offset);
    void write32(uint32_t offset, uint32_t val);
    uint32_t mask32(uint32_t offset, uint32_t mask, uint32_t val);
    bool wait_eq(uint32_t offset, uint32_t mask, uint32_t val);
    bool wait_ne(uint32_t offset, uint32_t mask, uint32_t val);
    template<typename Functor>
    bool wait_cb(uint32_t offset, uint32_t mask, uint32_t val, const Functor& func) {
#if !defined(NDEBUG)
        uint64_t counter = 0;
#endif
        do {
            if (func((read32(offset) & mask), val)) {
                return true;
            }
#if !defined(NDEBUG)
            ++counter;
            if ((counter % 10000000ULL) == 0) {
                A3_LOG("wait stop count %" PRIX64 "\n", counter);
                backward::StackTrace st;
                backward::Printer printer;
                st.load_here(32);
                printer.address = true;
                printer.print(st, stderr);
                return false;
            }
#endif
        } while (true);
        return false;
    }
    uint32_t read16(uint32_t offset);
    void write16(uint32_t offset, uint16_t val);
    uint32_t read8(uint32_t offset);
    void write8(uint32_t offset, uint8_t val);

 private:
    mutex_t::scoped_lock lock_;
};

inline uint32_t read32(uint32_t offset) {
    accessor regs;
    return regs.read32(offset);
}

inline void write32(uint32_t offset, uint32_t val) {
    accessor regs;
    regs.write32(offset, val);
}

inline uint32_t mask32(uint32_t offset, uint32_t mask, uint32_t val) {
    accessor regs;
    return regs.mask32(offset, mask, val);
}

inline bool wait_eq(uint32_t offset, uint32_t mask, uint32_t val) {
    accessor regs;
    return regs.wait_eq(offset, mask, val);
}

inline bool wait_ne(uint32_t offset, uint32_t mask, uint32_t val) {
    accessor regs;
    return regs.wait_ne(offset, mask, val);
}

} }  // namespace a3::registers
#endif  // A3_REGISTERS_H_
/* vim: set sw=4 ts=4 et tw=80 : */
