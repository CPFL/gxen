#ifndef CROSS_REGISTERS_H_
#define CROSS_REGISTERS_H_
#include <boost/noncopyable.hpp>
#include "cross_device.h"
namespace cross {

class registers_accessor : private boost::noncopyable {
 public:
    explicit registers_accessor();
    uint32_t read32(uint32_t offset);
    void write32(uint32_t offset, uint32_t val);

 private:
    device::mutex::scoped_lock lock_;
};

}  // namespace cross
#endif  // CROSS_REGISTERS_H_
/* vim: set sw=4 ts=4 et tw=80 : */
