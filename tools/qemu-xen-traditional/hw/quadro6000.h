#ifndef HW_QUADRO6000_H_
#define HW_QUADRO6000_H_

#include "nvreg.h"
#include "nouveau_reg.h"

#define QUADRO6000_VENDOR 0x10DE
#define QUADRO6000_DEVICE 0x6D8
#define QUADRO6000_COMMAND 0x07
#define QUADRO6000_REVISION 0xA3
#define QUADRO6000_REG0 0x0C0C00A3UL

#define Q6_PRINTF(fmt, arg...) do {\
    printf("[Quadro6000] %s:%d - " fmt, __func__, __LINE__, ##arg);\
} while (0)

void pci_quadro6000_init(PCIBus* bus);

#endif  // HW_QUADRO6000_H_
/* vim: set sw=4 ts=4 et tw=80 : */
