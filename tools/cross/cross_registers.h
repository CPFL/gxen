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

} }  // namespace cross::registers
#endif  // CROSS_REGISTERS_H_
/* vim: set sw=4 ts=4 et tw=80 : */
