#ifndef A3_PV_SLOT_H_
#define A3_PV_SLOT_H_
#include <cstdio>
#include <cassert>
#include <stdint.h>
#include "a3.h"
namespace a3 {

#define NOUVEAU_PV_REG_BAR 4
#define NOUVEAU_PV_SLOT_SIZE 0x1000ULL
#define NOUVEAU_PV_SLOT_NUM 64ULL
#define NOUVEAU_PV_SLOT_TOTAL (NOUVEAU_PV_SLOT_SIZE * NOUVEAU_PV_SLOT_NUM)
#define NOUVEAU_PV_BATCH_SIZE 128ULL

#define A3_PV_OPS_LIST(V)\
    V(NOUVEAU_PV_OP_SET_PGD)\
    V(NOUVEAU_PV_OP_MAP_PGT)\
    V(NOUVEAU_PV_OP_MAP)\
    V(NOUVEAU_PV_OP_MAP_BATCH)\
    V(NOUVEAU_PV_OP_MAP_SG_BATCH)\
    V(NOUVEAU_PV_OP_UNMAP_BATCH)\
    V(NOUVEAU_PV_OP_VM_FLUSH)\
    V(NOUVEAU_PV_OP_MEM_ALLOC)\
    V(NOUVEAU_PV_OP_MEM_FREE)\
    V(NOUVEAU_PV_OP_BAR3_PGT)\

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
