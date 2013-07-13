#ifndef HW_NVC0_NVC0_MAIN_H_
#define HW_NVC0_NVC0_MAIN_H_

#include "nvc0.h"
#include "nvc0_para_virt.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "pass-through.h"

struct pt_dev * pci_nvc0_init(PCIBus *bus, const char *e_dev_name);

#ifdef __cplusplus
}
#endif
#endif  // HW_NVC0_NVC0_MAIN_H_
/* vim: set sw=4 ts=4 et tw=80 : */
