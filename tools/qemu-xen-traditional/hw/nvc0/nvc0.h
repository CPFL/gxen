#ifndef HW_NVC0_NVC0_H_
#define HW_NVC0_NVC0_H_

#include <pciaccess.h>
#include "hw.h"
#include "pc.h"
#include "irq.h"
#include "pci.h"
#include "pci/header.h"
#include "pci/pci.h"
#include "pass-through.h"
#include "nvc0/nvreg.h"
#include "nvc0/nouveau_reg.h"
#include "nvc0/nvc0_channel.h"

#define NVC0_VENDOR 0x10DE
#define NVC0_DEVICE 0x6D8
#define NVC0_COMMAND 0x07
#define NVC0_REVISION 0xA3
#define NVC0_REG0 0x0C0C00A3UL

#define NVC0_PRINTF(fmt, arg...) do {\
    printf("[NVC0] %s:%d - " fmt, __func__, __LINE__, ##arg);\
} while (0)

typedef struct nvc0_bar {
    int io_index;     //  io_index in qemu
    uint32_t addr;    //  MMIO GPA
    uint32_t size;    //  MMIO memory size
    int type;
    uint8_t* space;   //  workspace memory
    uint8_t* real;    //  MMIO HVA
} nvc0_bar_t;

typedef struct nvc0_state {
    struct pt_dev pt_dev;
    nvc0_bar_t bar[6];
    struct pci_dev* real;           // from pci.h
    struct pci_device* access;      // from pciaccess.h. Basically we use this to access device.
    uint32_t guest;                 // guest index
    uint32_t log;                   // log flag
    nvc0_pfifo_t pfifo;             // pfifo
} nvc0_state_t;

struct pt_dev * pci_nvc0_init(PCIBus *e_bus,
        const char *e_dev_name, int e_devfn, uint8_t r_bus, uint8_t r_dev,
        uint8_t r_func, uint32_t machine_irq, struct pci_access *pci_access,
        char *opt);

extern int nvc0_enabled;

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

static inline uint32_t nvc0_channel_get_phys_id(nvc0_state_t* state, uint32_t virt) {
    return virt + state->guest * NVC0_CHANNELS_SHIFT;
}

static inline uint32_t nvc0_channel_get_virt_id(nvc0_state_t* state, uint32_t phys) {
    return phys - state->guest * NVC0_CHANNELS_SHIFT;
}

#endif  // HW_NVC0_NVC0_H_
/* vim: set sw=4 ts=4 et tw=80 : */
