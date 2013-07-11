#ifndef A3_H_
#define A3_H_
#include <cstdio>
#include <cassert>
#include <stdint.h>
#include <boost/interprocess/ipc/message_queue.hpp>
namespace a3 {

#define A3_VERSION "0.0.1"
#define A3_ENDPOINT "/tmp/a3_endpoint"
#define A3_CHANNELS 128
#define A3_DOMAIN_CHANNELS (A3_CHANNELS / 2)
#define A3_1G 0x40000000ULL
#define A3_2G (A3_1G * 2)

#define A3_MEMORY_CTL_NUM 1
#define A3_MEMORY_CTL_PART (A3_1G / 2)
#define A3_MEMORY_SIZE (A3_MEMORY_CTL_PART * A3_MEMORY_CTL_NUM)

#define A3_GPC_BCAST(r) (0x418000 + (r))
#define A3_BAR0_SIZE (32ULL << 20)
#define A3_BAR4_SIZE (0x1000ULL)
#define A3_GUEST_DATA_SIZE (0x1000ULL * 4)

#if defined(NDEBUG)
    #define A3_FPRINTF(stream, fmt, args...) do { } while (0)
#else
    #define A3_FPRINTF(stream, fmt, args...) do {\
            std::fprintf(stream, "[A3] %s:%d - " fmt, __func__, __LINE__, ##args);\
            std::fflush(stream);\
        } while (0)
#endif

#define A3_LOG(fmt, args...) A3_FPRINTF(stdout, fmt, ##args)

#define A3_UNREACHABLE() assert(0)

namespace interprocess = boost::interprocess;

class command {
 public:
    enum type_t {
        TYPE_INIT,
        TYPE_WRITE,
        TYPE_READ,
        TYPE_UTILITY,
        TYPE_BAR3
    };

    enum bar_t {
        BAR0 = 0,
        BAR1 = 1,
        BAR3 = 3,
        BAR4 = 4
    };

    enum utility_t {
        UTILITY_PGRAPH_STATUS = 0,
        UTILITY_REGISTER_READ
    };

    uint32_t type;
    uint32_t value;
    uint32_t offset;
    uint8_t  u8[4];

    inline bar_t bar() const { return static_cast<bar_t>(u8[0]); }
    inline std::size_t size() const { return u8[1]; }
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

}  // namespace a3
#endif  // A3_H_
/* vim: set sw=4 ts=4 et tw=80 : */
