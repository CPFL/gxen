#ifndef HW_NVC0_NVC0_MMIO_BAR1_H_
#define HW_NVC0_NVC0_MMIO_BAR1_H_
#include "nvc0.h"

// init
void nvc0_init_bar1(nvc0_state_t* state);

// read functions
uint32_t nvc0_mmio_bar1_readb(void *opaque, target_phys_addr_t addr);
uint32_t nvc0_mmio_bar1_readw(void *opaque, target_phys_addr_t addr);
uint32_t nvc0_mmio_bar1_readd(void *opaque, target_phys_addr_t addr);

// write functions
void nvc0_mmio_bar1_writeb(void *opaque, target_phys_addr_t addr, uint32_t val);
void nvc0_mmio_bar1_writew(void *opaque, target_phys_addr_t addr, uint32_t val);
void nvc0_mmio_bar1_writed(void *opaque, target_phys_addr_t addr, uint32_t val);

#endif  // HW_NVC0_NVC0_MMIO_BAR1_H_
/* vim: set sw=4 ts=4 et tw=80 : */
