/*
 * QEMU DEC 21154 PCI bridge
 *
 * Copyright (c) 2006-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "dec_pci.h"
#include "sysbus.h"
#include "pci.h"
#include "pci_host.h"
#include "pci_bridge.h"
#include "pci_internals.h"

/* debug DEC */
//#define DEBUG_DEC

#ifdef DEBUG_DEC
#define DEC_DPRINTF(fmt, ...)                               \
    do { printf("DEC: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DEC_DPRINTF(fmt, ...)
#endif

typedef struct DECState {
    SysBusDevice busdev;
    PCIHostState host_state;
} DECState;

static int dec_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return irq_num;
}

static PCIDeviceInfo dec_21154_pci_bridge_info = {
    .qdev.name = "dec-21154-p2p-bridge",
    .qdev.desc = "DEC 21154 PCI-PCI bridge",
    .qdev.size = sizeof(PCIBridge),
    .qdev.vmsd = &vmstate_pci_device,
    .qdev.reset = pci_bridge_reset,
    .init = pci_bridge_initfn,
    .exit = pci_bridge_exitfn,
    .vendor_id = PCI_VENDOR_ID_DEC,
    .device_id = PCI_DEVICE_ID_DEC_21154,
    .config_write = pci_bridge_write_config,
    .is_bridge = 1,
};

PCIBus *pci_dec_21154_init(PCIBus *parent_bus, int devfn)
{
    PCIDevice *dev;
    PCIBridge *br;

    dev = pci_create_multifunction(parent_bus, devfn, false,
                                   "dec-21154-p2p-bridge");
    br = DO_UPCAST(PCIBridge, dev, dev);
    pci_bridge_map_irq(br, "DEC 21154 PCI-PCI bridge", dec_map_irq);
    qdev_init_nofail(&dev->qdev);
    return pci_bridge_get_sec_bus(br);
}

static int pci_dec_21154_init_device(SysBusDevice *dev)
{
    DECState *s;

    s = FROM_SYSBUS(DECState, dev);

    memory_region_init_io(&s->host_state.conf_mem, &pci_host_conf_le_ops,
                          &s->host_state, "pci-conf-idx", 0x1000);
    memory_region_init_io(&s->host_state.data_mem, &pci_host_data_le_ops,
                          &s->host_state, "pci-data-idx", 0x1000);
    sysbus_init_mmio_region(dev, &s->host_state.conf_mem);
    sysbus_init_mmio_region(dev, &s->host_state.data_mem);
    return 0;
}

static int dec_21154_pci_host_init(PCIDevice *d)
{
    /* PCI2PCI bridge same values as PearPC - check this */
    return 0;
}

static PCIDeviceInfo dec_21154_pci_host_info = {
    .qdev.name = "dec-21154",
    .qdev.size = sizeof(PCIDevice),
    .init      = dec_21154_pci_host_init,
    .vendor_id = PCI_VENDOR_ID_DEC,
    .device_id = PCI_DEVICE_ID_DEC_21154,
    .revision = 0x02,
    .class_id = PCI_CLASS_BRIDGE_PCI,
    .is_bridge  = 1,
};

static void dec_register_devices(void)
{
    sysbus_register_dev("dec-21154", sizeof(DECState),
                        pci_dec_21154_init_device);
    pci_qdev_register(&dec_21154_pci_host_info);
    pci_qdev_register(&dec_21154_pci_bridge_info);
}

device_init(dec_register_devices)
