#ifndef A3_CHIPSET_H_
#define A3_CHIPSET_H_
#include <cstdint>
#include <boost/noncopyable.hpp>
#include "assertion.h"
namespace a3 {

enum struct card {
    UNINITIALIZED = 0,
    NVC0,
    NVE0
};
typedef card card_type_t;

class chipset_t : private boost::noncopyable {
 public:
    chipset_t(uint32_t boot0);
    inline card_type_t type() const {
        ASSERT(type_ != card::UNINITIALIZED);
        return type_;
    }
    inline uint32_t detail() const {
        ASSERT(type_ != card::UNINITIALIZED);
        return value_ & 0xff;
    }

 private:
    uint32_t value_;
    card_type_t type_;
};

}  // namespace a3
#endif  // A3_CHIPSET_H_
/* vim: set sw=4 ts=4 et tw=80 : */
