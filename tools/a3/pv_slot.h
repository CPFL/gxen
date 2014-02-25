#ifndef A3_PV_SLOT_H_
#define A3_PV_SLOT_H_
#include <cstdio>
#include <cstdint>
#include "a3.h"
#include "assertion.h"
namespace a3 {

/* PV OPS */
enum pv_ops_t {
#define V(name) name,
    A3_PV_OPS_LIST(V)
#undef V
};

static const char* const kPV_OPS_STRING[] = {
#define V(name) #name,
    A3_PV_OPS_LIST(V)
#undef V
};


struct slot_t {
    union {
        uint8_t   u8[NOUVEAU_PV_SLOT_SIZE / sizeof(uint8_t) ];
        uint16_t u16[NOUVEAU_PV_SLOT_SIZE / sizeof(uint16_t)];
        uint32_t u32[NOUVEAU_PV_SLOT_SIZE / sizeof(uint32_t)];
        uint64_t u64[NOUVEAU_PV_SLOT_SIZE / sizeof(uint64_t)];
    };
};

}  // namespace a3
#endif  // A3_PV_SLOT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
