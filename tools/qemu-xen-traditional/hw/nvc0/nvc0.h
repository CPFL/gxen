#ifndef HW_NVC0_NVC0_H_
#define HW_NVC0_NVC0_H_

#include <pciaccess.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include "nvc0/nvreg.h"
#include "nvc0/nouveau_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NVC0_VENDOR 0x10DE
#define NVC0_DEVICE 0x6D8
#define NVC0_COMMAND 0x07
#define NVC0_REVISION 0xA3
#define NVC0_REG0 0x0C0C00A3UL

#define NVC0_CHANNELS 128
#define NVC0_CHANNELS_SHIFT 64
#define NVC0_USER_VMA_CHANNEL 0x1000

#define NVC0_PRINTF(fmt, args...) do {\
    printf("[NVC0] %s:%d - " fmt, __func__, __LINE__, ##args);\
} while (0)

#define NVC0_LOG(state, fmt, args...) do {\
    if (state->log) {\
        printf("[NVC0] %s:%d - " fmt, __func__, __LINE__, ##args);\
    }\
} while (0)

// FIXME
typedef uint64_t target_phys_addr_t;

typedef struct nvc0_pfifo {
    size_t size;
    uint32_t user_vma_enabled;
    uint64_t user_vma;        // user_vma channel vm addr value
    uint64_t playlist;
    uint32_t playlist_count;
} nvc0_pfifo_t;

typedef struct nvc0_bar {
    int io_index;     //  io_index in qemu
    uint32_t addr;    //  MMIO GPA
    uint32_t size;    //  MMIO memory size
    int type;
    uint8_t* space;   //  workspace memory
    uint8_t* real;    //  MMIO HVA
} nvc0_bar_t;


typedef struct {
    struct pt_dev* device;
    nvc0_bar_t bar[7];
    struct pci_dev* real;           // from pci.h
    struct pci_device* access;      // from pciaccess.h. Basically we use this to access device.
    uint32_t guest;                 // guest index
    uint32_t log;                   // log flag
    nvc0_pfifo_t pfifo;             // pfifo
    void* priv;                     // store C++ NVC0 context
} nvc0_state_t;

// convert pt_dev to nvc0_state_t
nvc0_state_t* nvc0_state(void* opaque);

// construct NVC0 context
void nvc0_context_init(nvc0_state_t* state);

// nvc0 graph
#define GPC_MAX 4
#define TP_MAX 32

#define ROP_BCAST(r)     (0x408800 + (r))
#define ROP_UNIT(u, r)   (0x410000 + (u) * 0x400 + (r))
#define GPC_BCAST(r)     (0x418000 + (r))
#define GPC_UNIT(t, r)   (0x500000 + (t) * 0x8000 + (r))
#define TP_UNIT(t, m, r) (0x504000 + (t) * 0x8000 + (m) * 0x800 + (r))

// MMIO accesses
// only considers little endianess
static inline uint8_t nvc0_read8(const volatile void *addr) {
    return *(const volatile uint8_t*) addr;
}

static inline uint16_t nvc0_read16(const volatile void *addr) {
    return *(const volatile uint16_t*) addr;
}

static inline uint32_t nvc0_read32(const volatile void *addr) {
    return *(const volatile uint32_t*) addr;
}

static inline void nvc0_write8(uint8_t b, volatile void *addr) {
    *(volatile uint8_t*) addr = b;
}

static inline void nvc0_write16(uint16_t b, volatile void *addr) {
    *(volatile uint16_t*) addr = b;
}

static inline void nvc0_write32(uint32_t b, volatile void *addr) {
    *(volatile uint32_t*) addr = b;
}

// channel function

uint32_t nvc0_domid(void);

#ifdef __cplusplus
}
#endif

#endif  // HW_NVC0_NVC0_H_
/* vim: set sw=4 ts=4 et tw=80 : */
