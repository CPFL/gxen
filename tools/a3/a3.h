#ifndef A3_A3_H_
#define A3_A3_H_
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/static_assert.hpp>
#include "config.h"
#include "flags.h"
#include "assertion.h"
namespace a3 {

inline unsigned long long print_count() {
    static std::atomic<uint64_t> g_counter(0u);
    return g_counter.fetch_add(1u);
}

#if defined(NDEBUG)
    #define A3_RAW_FPRINTF(stream, fmt, args...) do { } while (0)
#else
    #define A3_RAW_FPRINTF(stream, fmt, args...) do {\
            std::fprintf(stream, "[A3][%08llu] " fmt, ::a3::print_count(), ##args);\
            std::fflush(stream);\
        } while (0)
#endif

#define A3_FPRINTF(stream, fmt, args...) \
    A3_RAW_FPRINTF(stream, "%s:%d - " fmt, __func__, __LINE__, ##args)

#define A3_FATAL(stream, fmt, args...) do {\
        std::fprintf(stream, "[A3][%08llu] %s:%d - " fmt, ::a3::print_count(), __func__, __LINE__, ##args);\
        std::fflush(stream);\
    } while (0)

#define A3_LOG(fmt, args...) A3_FPRINTF(stdout, fmt, ##args)

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
#endif  // A3_A3_H_
/* vim: set sw=4 ts=4 et tw=80 : */
