#ifndef HW_NVC0_NVC0_VM_H_
#define HW_NVC0_NVC0_VM_H_
#include "nvc0.h"

void nvc0_vm_init(nvc0_state_t* state);
uint32_t nvc0_vm_bar1_read(nvc0_state_t* state, target_phys_addr_t offset);
void nvc0_vm_bar1_write(nvc0_state_t* state, target_phys_addr_t offset, uint32_t value);
uint32_t nvc0_vm_pramin_read(nvc0_state_t* state, target_phys_addr_t offset);
void nvc0_vm_pramin_write(nvc0_state_t* state, target_phys_addr_t offset, uint32_t value);

#endif  // HW_NVC0_NVC0_VM_H_
/* vim: set sw=4 ts=4 et tw=80 : */
