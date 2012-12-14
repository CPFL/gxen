#ifndef HW_QUADRO6000_H_
#define HW_QUADRO6000_H_

#include <pciaccess.h>
#include "nvreg.h"
#include "nouveau_reg.h"
#include "hw.h"
#include "pc.h"
#include "irq.h"
#include "pci.h"
#include "pci/header.h"
#include "pci/pci.h"
#include "pass-through.h"

#define QUADRO6000_VENDOR 0x10DE
#define QUADRO6000_DEVICE 0x6D8
#define QUADRO6000_COMMAND 0x07
#define QUADRO6000_REVISION 0xA3
#define QUADRO6000_REG0 0x0C0C00A3UL

#define Q6_PRINTF(fmt, arg...) do {\
    printf("[Quadro6000] %s:%d - " fmt, __func__, __LINE__, ##arg);\
} while (0)

typedef struct quadro6000_bar {
    int io_index;     //  io_index in qemu
    uint32_t addr;    //  MMIO GPA
    uint32_t size;    //  MMIO memory size
    int type;
    uint8_t* space;   //  workspace memory
    uint8_t* real;    //  MMIO HVA
} quadro6000_bar_t;

typedef struct quadro6000_state {
    struct pt_dev pt_dev;
    quadro6000_bar_t bar[6];
    struct pci_dev* real;           // from pci.h
    struct pci_device* access;      // from pciaccess.h. Basically we use this to access device.
} quadro6000_state_t;

struct pt_dev * pci_quadro6000_init(PCIBus *e_bus,
        const char *e_dev_name, int e_devfn, uint8_t r_bus, uint8_t r_dev,
        uint8_t r_func, uint32_t machine_irq, struct pci_access *pci_access,
        char *opt);

extern int quadro6000_enabled;

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
static inline uint8_t quadro6000_read8(const volatile void *addr) {
    return *(const volatile uint8_t*) addr;
}

static inline uint16_t quadro6000_read16(const volatile void *addr) {
    return *(const volatile uint16_t*) addr;
}

static inline uint32_t quadro6000_read32(const volatile void *addr) {
    return *(const volatile uint32_t*) addr;
}

static inline void quadro6000_write8(uint8_t b, volatile void *addr) {
    *(volatile uint8_t*) addr = b;
}

static inline void quadro6000_write16(uint16_t b, volatile void *addr) {
    *(volatile uint16_t*) addr = b;
}

static inline void quadro6000_write32(uint32_t b, volatile void *addr) {
    *(volatile uint32_t*) addr = b;
}

#endif  // HW_QUADRO6000_H_
/* vim: set sw=4 ts=4 et tw=80 : */
