/*
 * Copyright (C) 2010       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "hw/xen.h"

void xenstore_store_pv_console_info(int i, CharDriverState *chr)
{
}

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    return -1;
}

void xen_piix3_set_irq(void *opaque, int irq_num, int level)
{
}

void xen_piix_pci_write_config_client(uint32_t address, uint32_t val, int len)
{
}

void xen_cmos_set_s3_resume(void *opaque, int irq, int level)
{
}

void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size)
{
}

qemu_irq *xen_interrupt_controller_init(void)
{
    return NULL;
}

int xen_init(void)
{
    return -ENOSYS;
}
