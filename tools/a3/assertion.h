#ifndef A3_ASSERTION_H_
#define A3_ASSERTION_H_
#include <cassert>

#ifndef NDEBUG
    #if defined(QEMU_DM_NVC0)
        #define ASSERT(cond) assert(cond)
    #else
        #include "backward.hpp"
        #define ASSERT(cond) do {\
            if (!(cond)) {\
                backward::StackTrace st;\
                st.load_here(32);\
                backward::Printer p;\
                p.object = true;\
                p.color = true;\
                p.address = true;\
                p.print(st, stderr);\
                assert((cond) && #cond);\
            }\
        } while (0)
    #endif
#else
    #define ASSERT(cond) do { } while (0)
#endif

#define A3_UNREACHABLE() ASSERT(0)

#endif  // A3_ASSERTION_H_
/* vim: set sw=4 ts=4 et tw=80 : */
