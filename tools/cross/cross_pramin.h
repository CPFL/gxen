#ifndef CROSS_PRAMIN_H_
#define CROSS_PRAMIN_H_
#include <boost/noncopyable.hpp>
#include "cross_device.h"
#include "cross_registers.h"
namespace cross {
namespace pramin {

class accessor : private boost::noncopyable {
 public:
    explicit accessor();
    ~accessor();
    uint32_t read32(uint64_t addr);
    void write32(uint64_t addr, uint32_t val);

 private:
    void change_current(uint64_t addr);

    registers::accessor regs_;
    uint32_t old_;
};

inline uint32_t read32(uint64_t addr) {
    accessor pramin;
    return pramin.read32(addr);
}

inline void write32(uint64_t addr, uint32_t val) {
    accessor pramin;
    pramin.write32(addr, val);
}

} }  // namespace cross::pramin
#endif  // CROSS_PRAMIN_H_
/* vim: set sw=4 ts=4 et tw=80 : */
