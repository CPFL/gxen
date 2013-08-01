#ifndef HW_NVC0_NVC0_MMIO_ROM_H_
#define HW_NVC0_NVC0_MMIO_ROM_H_

#include "nvc0.h"

#ifdef __cplusplus
extern "C" {
#endif

// init
void nvc0_init_rom(nvc0_state_t* state);

// read functions
uint32_t nvc0_mmio_rom_readb(void *opaque, target_phys_addr_t addr);
uint32_t nvc0_mmio_rom_readw(void *opaque, target_phys_addr_t addr);
uint32_t nvc0_mmio_rom_readd(void *opaque, target_phys_addr_t addr);

// write functions
void nvc0_mmio_rom_writeb(void *opaque, target_phys_addr_t addr, uint32_t val);
void nvc0_mmio_rom_writew(void *opaque, target_phys_addr_t addr, uint32_t val);
void nvc0_mmio_rom_writed(void *opaque, target_phys_addr_t addr, uint32_t val);

#ifdef __cplusplus
}
#endif

#endif  // HW_NVC0_NVC0_MMIO_ROM_H_
/* vim: set sw=4 ts=4 et tw=80 : */
