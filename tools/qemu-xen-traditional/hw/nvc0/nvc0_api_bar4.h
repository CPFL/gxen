#ifndef HW_NVC0_API_BAR4_H_
#define HW_NVC0_API_BAR4_H_
#include "nvc0.h"

#ifdef __cplusplus
extern "C" {
#endif

// read functions
uint32_t nvc0_api_mmio_bar4_readb(void *opaque, target_phys_addr_t addr);
uint32_t nvc0_api_mmio_bar4_readw(void *opaque, target_phys_addr_t addr);
uint32_t nvc0_api_mmio_bar4_readd(void *opaque, target_phys_addr_t addr);

// write functions
void nvc0_api_mmio_bar4_writeb(void *opaque, target_phys_addr_t addr, uint32_t val);
void nvc0_api_mmio_bar4_writew(void *opaque, target_phys_addr_t addr, uint32_t val);
void nvc0_api_mmio_bar4_writed(void *opaque, target_phys_addr_t addr, uint32_t val);

#ifdef __cplusplus
}
#endif
#endif  // HW_NVC0_API_BAR4_H_
/* vim: set sw=4 ts=4 et tw=80 : */
