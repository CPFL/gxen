#ifndef QEMU_PCI_INTERNALS_H
#define QEMU_PCI_INTERNALS_H

/*
 * This header files is private to pci.c and pci_bridge.c
 * So following structures are opaque to others and shouldn't be
 * accessed.
 *
 * For pci-to-pci bridge needs to include this header file to embed
 * PCIBridge in its structure or to get sizeof(PCIBridge),
 * However, they shouldn't access those following members directly.
 * Use accessor function in pci.h, pci_bridge.h
 */

extern struct BusInfo pci_bus_info;

struct PCIBus {
    BusState qbus;
    uint8_t devfn_min;
    pci_set_irq_fn set_irq;
    pci_map_irq_fn map_irq;
    pci_hotplug_fn hotplug;
    DeviceState *hotplug_qdev;
    void *irq_opaque;
    PCIDevice *devices[PCI_SLOT_MAX * PCI_FUNC_MAX];
    PCIDevice *parent_dev;
    MemoryRegion *address_space_mem;
    MemoryRegion *address_space_io;

    QLIST_HEAD(, PCIBus) child; /* this will be replaced by qdev later */
    QLIST_ENTRY(PCIBus) sibling;/* this will be replaced by qdev later */

    /* The bus IRQ state is the logical OR of the connected devices.
       Keep a count of the number of devices with raised IRQs.  */
    int nirq;
    int *irq_count;
};

struct PCIBridge {
    PCIDevice dev;

    /* private member */
    PCIBus sec_bus;
    /*
     * Memory regions for the bridge's address spaces.  These regions are not
     * directly added to system_memory/system_io or its descendants.
     * Bridge's secondary bus points to these, so that devices
     * under the bridge see these regions as its address spaces.
     * The regions are as large as the entire address space -
     * they don't take into account any windows.
     */
    MemoryRegion address_space_mem;
    MemoryRegion address_space_io;
    /*
     * Aliases for each of the address space windows that the bridge
     * can forward. Mapped into the bridge's parent's address space,
     * as subregions.
     */
    MemoryRegion alias_pref_mem;
    MemoryRegion alias_mem;
    MemoryRegion alias_io;
    pci_map_irq_fn map_irq;
    const char *bus_name;
};

#endif /* QEMU_PCI_INTERNALS_H */
