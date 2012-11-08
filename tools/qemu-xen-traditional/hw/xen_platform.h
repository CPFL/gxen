#ifndef XEN_PLATFORM_H
#define XEN_PLATFORM_H

#include "pci.h"

void pci_xen_platform_init(PCIBus *bus);
void platform_fixed_ioport_init(void);

#endif
