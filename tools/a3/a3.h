#ifndef A3_H_
#define A3_H_
#include <cstdio>
#include <cassert>
#include <stdint.h>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/static_assert.hpp>
#include "a3_config.h"
#include "a3_flags.h"
namespace a3 {

#if defined(NDEBUG)
    #define A3_FPRINTF(stream, fmt, args...) do { } while (0)
#else
    #define A3_FPRINTF(stream, fmt, args...) do {\
            std::fprintf(stream, "[A3] %s:%d - " fmt, __func__, __LINE__, ##args);\
            std::fflush(stream);\
        } while (0)
#endif

#define A3_FATAL(stream, fmt, args...) do {\
        std::fprintf(stream, "[A3] %s:%d - " fmt, __func__, __LINE__, ##args);\
        std::fflush(stream);\
    } while (0)

#define A3_LOG(fmt, args...) A3_FPRINTF(stdout, fmt, ##args)

#define A3_UNREACHABLE() assert(0)

BOOST_STATIC_ASSERT(A3_MEMORY_CTL_NUM != 0);

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
        UTILITY_REGISTER_READ,
        UTILITY_CLEAR_SHADOWING_UTILIZATION
    };

    uint32_t type;
    uint32_t value;
    uint32_t offset;
    uint8_t  u8[4];

    inline bar_t bar() const { return static_cast<bar_t>(u8[0]); }
    inline std::size_t size() const { return u8[1]; }

    // instrumentations
    void check_tlb() { u8[2] = 0xab; }
    bool is_tlb() const { return u8[2] == 0xab; }
    void check_shadowing() { u8[3] = 0xbc; }
    bool is_shadowing() const { return u8[3] == 0xbc; }
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
