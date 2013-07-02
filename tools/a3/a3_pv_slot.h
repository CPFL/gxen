#ifndef A3_PV_SLOT_H_
#define A3_PV_SLOT_H_
#include <cstdio>
#include <cassert>
#include <stdint.h>
#include "a3.h"
namespace a3 {

#define NOUVEAU_PV_REG_BAR 4
#define NOUVEAU_PV_SLOT_SIZE 0x100ULL
#define NOUVEAU_PV_SLOT_NUM 64ULL
#define NOUVEAU_PV_SLOT_TOTAL (NOUVEAU_PV_SLOT_SIZE * NOUVEAU_PV_SLOT_NUM)

/* PV OPS */
enum {
	NOUVEAU_PV_OP_SET_PGD,
	NOUVEAU_PV_OP_MAP_PGT,
	NOUVEAU_PV_OP_MAP,
	NOUVEAU_PV_OP_VM_FLUSH,
	NOUVEAU_PV_OP_MEM_ALLOC,
};

struct slot_t {
	union {
		u8   u8[NOUVEAU_PV_SLOT_SIZE / sizeof(u8) ];
		u16 u16[NOUVEAU_PV_SLOT_SIZE / sizeof(u16)];
		u32 u32[NOUVEAU_PV_SLOT_SIZE / sizeof(u32)];
		u64 u64[NOUVEAU_PV_SLOT_SIZE / sizeof(u64)];
	};
};

}  // namespace a3
#endif  // A3_PV_SLOT_H_
/* vim: set sw=4 ts=4 et tw=80 : */
