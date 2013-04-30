#ifndef A3_PRAMIN_H_
#define A3_PRAMIN_H_
#include <boost/noncopyable.hpp>
#include "a3_device.h"
#include "a3_registers.h"
namespace a3 {
namespace pramin {

class accessor : private boost::noncopyable {
 public:
    explicit accessor();
    ~accessor();

    uint32_t read(uint64_t addr, std::size_t size);
    void write(uint64_t addr, uint32_t val, std::size_t size);

    uint32_t read32(uint64_t addr);
    void write32(uint64_t addr, uint32_t val);
    uint32_t read16(uint64_t addr);
    void write16(uint64_t addr, uint16_t val);
    uint32_t read8(uint64_t addr);
    void write8(uint64_t addr, uint8_t val);

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

} }  // namespace a3::pramin
#endif  // A3_PRAMIN_H_
/* vim: set sw=4 ts=4 et tw=80 : */
