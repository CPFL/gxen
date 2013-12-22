#ifndef HW_NVC0_NVC0_MMIO_H_
#define HW_NVC0_NVC0_MMIO_H_
#include "nvc0.h"

#ifdef __cplusplus
extern "C" {
#endif

void nvc0_mmio_init(nvc0_state_t* state);
void nvc0_api_paravirt_mmio_init(nvc0_state_t* state);
void nvc0_mmio_bar3_notify(nvc0_state_t* state);

// wrappers
static inline uint8_t nvc0_mmio_read8(void* ptr, ptrdiff_t offset) {
    return nvc0_read8(((uint8_t*)ptr) + offset);
}
static inline uint16_t nvc0_mmio_read16(void* ptr, ptrdiff_t offset) {
    return nvc0_read16(((uint8_t*)ptr) + offset);
}
static inline uint32_t nvc0_mmio_read32(void* ptr, ptrdiff_t offset) {
    return nvc0_read32(((uint8_t*)ptr) + offset);
}

static inline void nvc0_mmio_write8(void* ptr, ptrdiff_t offset, uint8_t data) {
    nvc0_write8(data, ((uint8_t*)ptr) + offset);
}
static inline void nvc0_mmio_write16(void* ptr, ptrdiff_t offset, uint16_t data) {
    nvc0_write16(data, ((uint8_t*)ptr) + offset);
}
static inline void nvc0_mmio_write32(void* ptr, ptrdiff_t offset, uint32_t data) {
    nvc0_write32(data, ((uint8_t*)ptr) + offset);
}

#ifdef __cplusplus
}
#endif
#endif  // HW_NVC0_NVC0_MMIO_H_
/* vim: set sw=4 ts=4 et tw=80 : */
