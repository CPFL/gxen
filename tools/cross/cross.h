#ifndef CROSS_H_
#define CROSS_H_
#include <cstdio>
#include <cassert>
#include <stdint.h>
namespace cross {

#define CROSS_ENDPOINT "/tmp/cross_endpoint"
#define CROSS_CHANNELS 128
#define CROSS_DOMAIN_CHANNELS (CROSS_CHANNELS / 2)
#define CROSS_1G 0x40000000ULL
#define CROSS_2G (CROSS_1G * 2)
#define CROSS_GPC_BCAST(r) (0x418000 + (r))

#if defined(NDEBUG)
    #define CROSS_FPRINTF(stream, fmt, args...) do { } (0)
#else
    #define CROSS_FPRINTF(stream, fmt, args...) do {\
            std::fprintf(stream, "[CROSS] %s:%d - " fmt, __func__, __LINE__, ##args);\
            std::fflush(stream);\
        } while (0)
#endif

#define CROSS_LOG(fmt, args...) CROSS_FPRINTF(stdout, fmt, ##args)

#define CROSS_UNREACHABLE() assert(0)

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
