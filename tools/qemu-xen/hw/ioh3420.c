/*
 * ioh3420.c
 * Intel X58 north bridge IOH
 * PCI Express root port device id 3420
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "pci_ids.h"
#include "msi.h"
#include "pcie.h"
#include "ioh3420.h"

#define PCI_DEVICE_ID_IOH_EPORT         0x3420  /* D0:F0 express mode */
#define PCI_DEVICE_ID_IOH_REV           0x2
#define IOH_EP_SSVID_OFFSET             0x40
#define IOH_EP_SSVID_SVID               PCI_VENDOR_ID_INTEL
#define IOH_EP_SSVID_SSID               0
#define IOH_EP_MSI_OFFSET               0x60
#define IOH_EP_MSI_SUPPORTED_FLAGS      PCI_MSI_FLAGS_MASKBIT
#define IOH_EP_MSI_NR_VECTOR            2
#define IOH_EP_EXP_OFFSET               0x90
#define IOH_EP_AER_OFFSET               0x100

/*
 * If two MSI vector are allocated, Advanced Error Interrupt Message Number
 * is 1. otherwise 0.
 * 17.12.5.10 RPERRSTS,  32:27 bit Advanced Error Interrupt Message Number.
 */
static uint8_t ioh3420_aer_vector(const PCIDevice *d)
{
    switch (msi_nr_vectors_allocated(d)) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 4:
    case 8:
    case 16:
    case 32:
    default:
        break;
    }
    abort();
    return 0;
}

static void ioh3420_aer_vector_update(PCIDevice *d)
{
    pcie_aer_root_set_vector(d, ioh3420_aer_vector(d));
}

static void ioh3420_write_config(PCIDevice *d,
                                   uint32_t address, uint32_t val, int len)
{
    uint32_t root_cmd =
        pci_get_long(d->config + d->exp.aer_cap + PCI_ERR_ROOT_COMMAND);

    pci_bridge_write_config(d, address, val, len);
    msi_write_config(d, address, val, len);
    ioh3420_aer_vector_update(d);
    pcie_cap_slot_write_config(d, address, val, len);
    pcie_aer_write_config(d, address, val, len);
    pcie_aer_root_write_config(d, address, val, len, root_cmd);
}

static void ioh3420_reset(DeviceState *qdev)
{
    PCIDevice *d = DO_UPCAST(PCIDevice, qdev, qdev);
    msi_reset(d);
    ioh3420_aer_vector_update(d);
    pcie_cap_root_reset(d);
    pcie_cap_deverr_reset(d);
    pcie_cap_slot_reset(d);
    pcie_aer_root_reset(d);
    pci_bridge_reset(qdev);
    pci_bridge_disable_base_limit(d);
}

static int ioh3420_initfn(PCIDevice *d)
{
    PCIBridge* br = DO_UPCAST(PCIBridge, dev, d);
    PCIEPort *p = DO_UPCAST(PCIEPort, br, br);
    PCIESlot *s = DO_UPCAST(PCIESlot, port, p);
    int rc;
    int tmp;

    rc = pci_bridge_initfn(d);
    if (rc < 0) {
        return rc;
    }

    pcie_port_init_reg(d);

    rc = pci_bridge_ssvid_init(d, IOH_EP_SSVID_OFFSET,
                               IOH_EP_SSVID_SVID, IOH_EP_SSVID_SSID);
    if (rc < 0) {
        goto err_bridge;
    }
    rc = msi_init(d, IOH_EP_MSI_OFFSET, IOH_EP_MSI_NR_VECTOR,
                  IOH_EP_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_64BIT,
                  IOH_EP_MSI_SUPPORTED_FLAGS & PCI_MSI_FLAGS_MASKBIT);
    if (rc < 0) {
        goto err_bridge;
    }
    rc = pcie_cap_init(d, IOH_EP_EXP_OFFSET, PCI_EXP_TYPE_ROOT_PORT, p->port);
    if (rc < 0) {
        goto err_msi;
    }
    pcie_cap_deverr_init(d);
    pcie_cap_slot_init(d, s->slot);
    pcie_chassis_create(s->chassis);
    rc = pcie_chassis_add_slot(s);
    if (rc < 0) {
        goto err_pcie_cap;
        return rc;
    }
    pcie_cap_root_init(d);
    rc = pcie_aer_init(d, IOH_EP_AER_OFFSET);
    if (rc < 0) {
        goto err;
    }
    pcie_aer_root_init(d);
    ioh3420_aer_vector_update(d);
    return 0;

err:
    pcie_chassis_del_slot(s);
err_pcie_cap:
    pcie_cap_exit(d);
err_msi:
    msi_uninit(d);
err_bridge:
    tmp = pci_bridge_exitfn(d);
    assert(!tmp);
    return rc;
}

static int ioh3420_exitfn(PCIDevice *d)
{
    PCIBridge* br = DO_UPCAST(PCIBridge, dev, d);
    PCIEPort *p = DO_UPCAST(PCIEPort, br, br);
    PCIESlot *s = DO_UPCAST(PCIESlot, port, p);

    pcie_aer_exit(d);
    pcie_chassis_del_slot(s);
    pcie_cap_exit(d);
    msi_uninit(d);
    return pci_bridge_exitfn(d);
}

PCIESlot *ioh3420_init(PCIBus *bus, int devfn, bool multifunction,
                         const char *bus_name, pci_map_irq_fn map_irq,
                         uint8_t port, uint8_t chassis, uint16_t slot)
{
    PCIDevice *d;
    PCIBridge *br;
    DeviceState *qdev;

    d = pci_create_multifunction(bus, devfn, multifunction, "ioh3420");
    if (!d) {
        return NULL;
    }
    br = DO_UPCAST(PCIBridge, dev, d);

    qdev = &br->dev.qdev;
    pci_bridge_map_irq(br, bus_name, map_irq);
    qdev_prop_set_uint8(qdev, "port", port);
    qdev_prop_set_uint8(qdev, "chassis", chassis);
    qdev_prop_set_uint16(qdev, "slot", slot);
    qdev_init_nofail(qdev);

    return DO_UPCAST(PCIESlot, port, DO_UPCAST(PCIEPort, br, br));
}

static const VMStateDescription vmstate_ioh3420 = {
    .name = "ioh-3240-express-root-port",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .post_load = pcie_cap_slot_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_PCIE_DEVICE(port.br.dev, PCIESlot),
        VMSTATE_STRUCT(port.br.dev.exp.aer_log, PCIESlot, 0,
                       vmstate_pcie_aer_log, PCIEAERLog),
        VMSTATE_END_OF_LIST()
    }
};

static PCIDeviceInfo ioh3420_info = {
    .qdev.name = "ioh3420",
    .qdev.desc = "Intel IOH device id 3420 PCIE Root Port",
    .qdev.size = sizeof(PCIESlot),
    .qdev.reset = ioh3420_reset,
    .qdev.vmsd = &vmstate_ioh3420,

    .is_express = 1,
    .is_bridge = 1,
    .config_write = ioh3420_write_config,
    .init = ioh3420_initfn,
    .exit = ioh3420_exitfn,
    .vendor_id = PCI_VENDOR_ID_INTEL,
    .device_id = PCI_DEVICE_ID_IOH_EPORT,
    .revision = PCI_DEVICE_ID_IOH_REV,

    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT8("port", PCIESlot, port.port, 0),
        DEFINE_PROP_UINT8("chassis", PCIESlot, chassis, 0),
        DEFINE_PROP_UINT16("slot", PCIESlot, slot, 0),
        DEFINE_PROP_UINT16("aer_log_max", PCIESlot,
                           port.br.dev.exp.aer_log.log_max,
                           PCIE_AER_LOG_MAX_DEFAULT),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void ioh3420_register(void)
{
    pci_qdev_register(&ioh3420_info);
}

device_init(ioh3420_register);

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 8
 *  indent-tab-mode: nil
 * End:
 */
