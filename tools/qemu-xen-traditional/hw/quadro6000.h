#ifndef HW_QUADRO6000_H_
#define HW_QUADRO6000_H_

#include "nvreg.h"
#include "nouveau_reg.h"
#include "pass-through.h"

#define QUADRO6000_VENDOR 0x10DE
#define QUADRO6000_DEVICE 0x6D8
#define QUADRO6000_COMMAND 0x07
#define QUADRO6000_REVISION 0xA3
#define QUADRO6000_REG0 0x0C0C00A3UL

#define Q6_PRINTF(fmt, arg...) do {\
    printf("[Quadro6000] %s:%d - " fmt, __func__, __LINE__, ##arg);\
} while (0)

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

#endif  // HW_QUADRO6000_H_
/* vim: set sw=4 ts=4 et tw=80 : */
