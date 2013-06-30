#ifndef HW_NVC0_NVC0_MMIO_BAR4_H_
#define HW_NVC0_NVC0_MMIO_BAR4_H_
#include "nvc0.h"

#ifdef __cplusplus
extern "C" {
#endif

// init
void nvc0_init_bar4(nvc0_state_t* state);

// read functions
uint32_t nvc0_mmio_bar4_readb(void *opaque, target_phys_addr_t addr);
uint32_t nvc0_mmio_bar4_readw(void *opaque, target_phys_addr_t addr);
uint32_t nvc0_mmio_bar4_readd(void *opaque, target_phys_addr_t addr);

// write functions
void nvc0_mmio_bar4_writeb(void *opaque, target_phys_addr_t addr, uint32_t val);
void nvc0_mmio_bar4_writew(void *opaque, target_phys_addr_t addr, uint32_t val);
void nvc0_mmio_bar4_writed(void *opaque, target_phys_addr_t addr, uint32_t val);

#ifdef __cplusplus
}
#endif
#endif  // HW_NVC0_NVC0_MMIO_BAR4_H_
/* vim: set sw=4 ts=4 et tw=80 : */
