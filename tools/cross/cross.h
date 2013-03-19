#ifndef CROSS_H_
#define CROSS_H_
#include <stdint.h>
namespace cross {

#define CROSS_ENDPOINT "/tmp/cross_endpoint"
#define CROSS_CHANNELS 128
#define CROSS_DOMAIN_CHANNELS (CROSS_CHANNELS / 2)
#define CROSS_2G 0x80000000ULL

class command {
 public:
    enum type_t {
        TYPE_INIT,
        TYPE_WRITE,
        TYPE_READ
    };

    enum bar_t {
        BAR0,
        BAR1,
        BAR3
    };

    uint32_t type;
    uint32_t value;
    uint32_t offset;
    uint32_t payload;
};

// Assuming little endianess
struct bdf {
    union {
        struct {
            uint16_t func : 3;
            uint16_t dev  : 5;
            uint16_t bus  : 8;
        };
        uint16_t raw;
    };
};

}  // namespace cross
#endif  // CROSS_H_
/* vim: set sw=4 ts=4 et tw=80 : */
