#ifndef CROSS_H_
#define CROSS_H_
namespace cross {

#define CROSS_ENDPOINT "/tmp/cross_endpoint"

class command {
 public:
    enum type_t {
        TYPE_INIT,
        TYPE_WRITE,
        TYPE_READ
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
