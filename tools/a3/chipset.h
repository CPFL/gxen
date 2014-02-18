#ifndef A3_CHIPSET_H_
#define A3_CHIPSET_H_
#include <cstdint>
namespace a3 {

enum struct card {
    NVC0,
    NVE0
};
typedef card card_type_t;

class chipset_t {
 public:
    chipset_t();
    chipset_t(uint32_t boot0);
    inline card_type_t type() const { return type_; }

 private:
    uint32_t value_;
    card_type_t type_;
};

}  // namespace a3
#endif  // A3_CHIPSET_H_
/* vim: set sw=4 ts=4 et tw=80 : */
