#ifndef HW_NVC0_NVC0_MAIN_H_
#define HW_NVC0_NVC0_MAIN_H_

#include "nvc0.h"
#include "nvc0_para_virt.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "pass-through.h"

struct pt_dev* pci_nvc0_init(PCIBus *e_bus,
        const char *e_dev_name, int e_devfn, uint8_t r_bus, uint8_t r_dev,
        uint8_t r_func, uint32_t machine_irq, struct pci_access *pci_access,
        char *opt);

#ifdef __cplusplus
}
#endif
#endif  // HW_NVC0_NVC0_MAIN_H_
/* vim: set sw=4 ts=4 et tw=80 : */
