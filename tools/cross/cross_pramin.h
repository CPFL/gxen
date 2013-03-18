#ifndef CROSS_PRAMIN_H_
#define CROSS_PRAMIN_H_
#include <boost/noncopyable.hpp>
#include "cross_device.h"
#include "cross_registers.h"
namespace cross {

class pramin_accessor : private boost::noncopyable {
 public:
    explicit pramin_accessor();
    ~pramin_accessor();
    uint32_t read32(uint64_t addr);
    void write32(uint64_t addr, uint32_t val);

 private:
    void change_current(uint64_t addr);

    registers_accessor regs_;
    uint32_t old_;
};

}  // namespace cross
#endif  // CROSS_PRAMIN_H_
/* vim: set sw=4 ts=4 et tw=80 : */
