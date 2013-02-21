#ifndef HW_NVC0_NVC0_PRAMIN_H_
#define HW_NVC0_NVC0_PRAMIN_H_
#include "nvc0.h"

#ifdef __cpp
extern "C" {
#endif

uint32_t nvc0_pramin_read32(nvc0_state_t* state, uint64_t addr);
void nvc0_pramin_write32(nvc0_state_t* state, uint64_t addr, uint32_t val);

#ifdef __cpp
}
#endif

#endif  // HW_NVC0_NVC0_PRAMIN_H_
/* vim: set sw=4 ts=4 et tw=80 : */
