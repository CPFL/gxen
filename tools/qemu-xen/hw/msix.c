/*
 * MSI-X device support
 *
 * This module includes support for MSI-X in pci devices.
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 *  Copyright (c) 2009, Red Hat Inc, Michael S. Tsirkin (mst@redhat.com)
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "hw.h"
#include "msix.h"
#include "pci.h"
#include "range.h"

#define MSIX_CAP_LENGTH 12

/* MSI enable bit and maskall bit are in byte 1 in FLAGS register */
#define MSIX_CONTROL_OFFSET (PCI_MSIX_FLAGS + 1)
#define MSIX_ENABLE_MASK (PCI_MSIX_FLAGS_ENABLE >> 8)
#define MSIX_MASKALL_MASK (PCI_MSIX_FLAGS_MASKALL >> 8)

/* How much space does an MSIX table need. */
/* The spec requires giving the table structure
 * a 4K aligned region all by itself. */
#define MSIX_PAGE_SIZE 0x1000
/* Reserve second half of the page for pending bits */
#define MSIX_PAGE_PENDING (MSIX_PAGE_SIZE / 2)
#define MSIX_MAX_ENTRIES 32


/* Flag for interrupt controller to declare MSI-X support */
int msix_supported;

/* Add MSI-X capability to the config space for the device. */
/* Given a bar and its size, add MSI-X table on top of it
 * and fill MSI-X capability in the config space.
 * Original bar size must be a power of 2 or 0.
 * New bar size is returned. */
static int msix_add_config(struct PCIDevice *pdev, unsigned short nentries,
                           unsigned bar_nr, unsigned bar_size)
{
    int config_offset;
    uint8_t *config;
    uint32_t new_size;

    if (nentries < 1 || nentries > PCI_MSIX_FLAGS_QSIZE + 1)
        return -EINVAL;
    if (bar_size > 0x80000000)
        return -ENOSPC;

    /* Add space for MSI-X structures */
    if (!bar_size) {
        new_size = MSIX_PAGE_SIZE;
    } else if (bar_size < MSIX_PAGE_SIZE) {
        bar_size = MSIX_PAGE_SIZE;
        new_size = MSIX_PAGE_SIZE * 2;
    } else {
        new_size = bar_size * 2;
    }

    pdev->msix_bar_size = new_size;
    config_offset = pci_add_capability(pdev, PCI_CAP_ID_MSIX,
                                       0, MSIX_CAP_LENGTH);
    if (config_offset < 0)
        return config_offset;
    config = pdev->config + config_offset;

    pci_set_word(config + PCI_MSIX_FLAGS, nentries - 1);
    /* Table on top of BAR */
    pci_set_long(config + PCI_MSIX_TABLE, bar_size | bar_nr);
    /* Pending bits on top of that */
    pci_set_long(config + PCI_MSIX_PBA, (bar_size + MSIX_PAGE_PENDING) |
                 bar_nr);
    pdev->msix_cap = config_offset;
    /* Make flags bit writable. */
    pdev->wmask[config_offset + MSIX_CONTROL_OFFSET] |= MSIX_ENABLE_MASK |
	    MSIX_MASKALL_MASK;
    pdev->msix_function_masked = true;
    return 0;
}

static uint64_t msix_mmio_read(void *opaque, target_phys_addr_t addr,
                               unsigned size)
{
    PCIDevice *dev = opaque;
    unsigned int offset = addr & (MSIX_PAGE_SIZE - 1) & ~0x3;
    void *page = dev->msix_table_page;

    return pci_get_long(page + offset);
}

static uint8_t msix_pending_mask(int vector)
{
    return 1 << (vector % 8);
}

static uint8_t *msix_pending_byte(PCIDevice *dev, int vector)
{
    return dev->msix_table_page + MSIX_PAGE_PENDING + vector / 8;
}

static int msix_is_pending(PCIDevice *dev, int vector)
{
    return *msix_pending_byte(dev, vector) & msix_pending_mask(vector);
}

static void msix_set_pending(PCIDevice *dev, int vector)
{
    *msix_pending_byte(dev, vector) |= msix_pending_mask(vector);
}

static void msix_clr_pending(PCIDevice *dev, int vector)
{
    *msix_pending_byte(dev, vector) &= ~msix_pending_mask(vector);
}

static bool msix_vector_masked(PCIDevice *dev, int vector, bool fmask)
{
    unsigned offset = vector * PCI_MSIX_ENTRY_SIZE + PCI_MSIX_ENTRY_VECTOR_CTRL;
    return fmask || dev->msix_table_page[offset] & PCI_MSIX_ENTRY_CTRL_MASKBIT;
}

static bool msix_is_masked(PCIDevice *dev, int vector)
{
    return msix_vector_masked(dev, vector, dev->msix_function_masked);
}

static void msix_handle_mask_update(PCIDevice *dev, int vector, bool was_masked)
{
    bool is_masked = msix_is_masked(dev, vector);
    if (is_masked == was_masked) {
        return;
    }

    if (!is_masked && msix_is_pending(dev, vector)) {
        msix_clr_pending(dev, vector);
        msix_notify(dev, vector);
    }
}

static void msix_update_function_masked(PCIDevice *dev)
{
    dev->msix_function_masked = !msix_enabled(dev) ||
        (dev->config[dev->msix_cap + MSIX_CONTROL_OFFSET] & MSIX_MASKALL_MASK);
}

/* Handle MSI-X capability config write. */
void msix_write_config(PCIDevice *dev, uint32_t addr,
                       uint32_t val, int len)
{
    unsigned enable_pos = dev->msix_cap + MSIX_CONTROL_OFFSET;
    int vector;
    bool was_masked;

    if (!range_covers_byte(addr, len, enable_pos)) {
        return;
    }

    was_masked = dev->msix_function_masked;
    msix_update_function_masked(dev);

    if (!msix_enabled(dev)) {
        return;
    }

    pci_device_deassert_intx(dev);

    if (dev->msix_function_masked == was_masked) {
        return;
    }

    for (vector = 0; vector < dev->msix_entries_nr; ++vector) {
        msix_handle_mask_update(dev, vector,
                                msix_vector_masked(dev, vector, was_masked));
    }
}

static void msix_mmio_write(void *opaque, target_phys_addr_t addr,
                            uint64_t val, unsigned size)
{
    PCIDevice *dev = opaque;
    unsigned int offset = addr & (MSIX_PAGE_SIZE - 1) & ~0x3;
    int vector = offset / PCI_MSIX_ENTRY_SIZE;
    bool was_masked;

    /* MSI-X page includes a read-only PBA and a writeable Vector Control. */
    if (vector >= dev->msix_entries_nr) {
        return;
    }

    was_masked = msix_is_masked(dev, vector);
    pci_set_long(dev->msix_table_page + offset, val);
    msix_handle_mask_update(dev, vector, was_masked);
}

static const MemoryRegionOps msix_mmio_ops = {
    .read = msix_mmio_read,
    .write = msix_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void msix_mmio_setup(PCIDevice *d, MemoryRegion *bar)
{
    uint8_t *config = d->config + d->msix_cap;
    uint32_t table = pci_get_long(config + PCI_MSIX_TABLE);
    uint32_t offset = table & ~(MSIX_PAGE_SIZE - 1);
    /* TODO: for assigned devices, we'll want to make it possible to map
     * pending bits separately in case they are in a separate bar. */

    memory_region_add_subregion(bar, offset, &d->msix_mmio);
}

static void msix_mask_all(struct PCIDevice *dev, unsigned nentries)
{
    int vector;
    for (vector = 0; vector < nentries; ++vector) {
        unsigned offset =
            vector * PCI_MSIX_ENTRY_SIZE + PCI_MSIX_ENTRY_VECTOR_CTRL;
        dev->msix_table_page[offset] |= PCI_MSIX_ENTRY_CTRL_MASKBIT;
    }
}

/* Initialize the MSI-X structures. Note: if MSI-X is supported, BAR size is
 * modified, it should be retrieved with msix_bar_size. */
int msix_init(struct PCIDevice *dev, unsigned short nentries,
              MemoryRegion *bar,
              unsigned bar_nr, unsigned bar_size)
{
    int ret;
    /* Nothing to do if MSI is not supported by interrupt controller */
    if (!msix_supported)
        return -ENOTSUP;

    if (nentries > MSIX_MAX_ENTRIES)
        return -EINVAL;

    dev->msix_entry_used = g_malloc0(MSIX_MAX_ENTRIES *
                                        sizeof *dev->msix_entry_used);

    dev->msix_table_page = g_malloc0(MSIX_PAGE_SIZE);
    msix_mask_all(dev, nentries);

    memory_region_init_io(&dev->msix_mmio, &msix_mmio_ops, dev,
                          "msix", MSIX_PAGE_SIZE);

    dev->msix_entries_nr = nentries;
    ret = msix_add_config(dev, nentries, bar_nr, bar_size);
    if (ret)
        goto err_config;

    dev->cap_present |= QEMU_PCI_CAP_MSIX;
    msix_mmio_setup(dev, bar);
    return 0;

err_config:
    dev->msix_entries_nr = 0;
    memory_region_destroy(&dev->msix_mmio);
    g_free(dev->msix_table_page);
    dev->msix_table_page = NULL;
    g_free(dev->msix_entry_used);
    dev->msix_entry_used = NULL;
    return ret;
}

static void msix_free_irq_entries(PCIDevice *dev)
{
    int vector;

    for (vector = 0; vector < dev->msix_entries_nr; ++vector) {
        dev->msix_entry_used[vector] = 0;
        msix_clr_pending(dev, vector);
    }
}

/* Clean up resources for the device. */
int msix_uninit(PCIDevice *dev, MemoryRegion *bar)
{
    if (!(dev->cap_present & QEMU_PCI_CAP_MSIX))
        return 0;
    pci_del_capability(dev, PCI_CAP_ID_MSIX, MSIX_CAP_LENGTH);
    dev->msix_cap = 0;
    msix_free_irq_entries(dev);
    dev->msix_entries_nr = 0;
    memory_region_del_subregion(bar, &dev->msix_mmio);
    memory_region_destroy(&dev->msix_mmio);
    g_free(dev->msix_table_page);
    dev->msix_table_page = NULL;
    g_free(dev->msix_entry_used);
    dev->msix_entry_used = NULL;
    dev->cap_present &= ~QEMU_PCI_CAP_MSIX;
    return 0;
}

void msix_save(PCIDevice *dev, QEMUFile *f)
{
    unsigned n = dev->msix_entries_nr;

    if (!(dev->cap_present & QEMU_PCI_CAP_MSIX)) {
        return;
    }

    qemu_put_buffer(f, dev->msix_table_page, n * PCI_MSIX_ENTRY_SIZE);
    qemu_put_buffer(f, dev->msix_table_page + MSIX_PAGE_PENDING, (n + 7) / 8);
}

/* Should be called after restoring the config space. */
void msix_load(PCIDevice *dev, QEMUFile *f)
{
    unsigned n = dev->msix_entries_nr;

    if (!(dev->cap_present & QEMU_PCI_CAP_MSIX)) {
        return;
    }

    msix_free_irq_entries(dev);
    qemu_get_buffer(f, dev->msix_table_page, n * PCI_MSIX_ENTRY_SIZE);
    qemu_get_buffer(f, dev->msix_table_page + MSIX_PAGE_PENDING, (n + 7) / 8);
    msix_update_function_masked(dev);
}

/* Does device support MSI-X? */
int msix_present(PCIDevice *dev)
{
    return dev->cap_present & QEMU_PCI_CAP_MSIX;
}

/* Is MSI-X enabled? */
int msix_enabled(PCIDevice *dev)
{
    return (dev->cap_present & QEMU_PCI_CAP_MSIX) &&
        (dev->config[dev->msix_cap + MSIX_CONTROL_OFFSET] &
         MSIX_ENABLE_MASK);
}

/* Size of bar where MSI-X table resides, or 0 if MSI-X not supported. */
uint32_t msix_bar_size(PCIDevice *dev)
{
    return (dev->cap_present & QEMU_PCI_CAP_MSIX) ?
        dev->msix_bar_size : 0;
}

/* Send an MSI-X message */
void msix_notify(PCIDevice *dev, unsigned vector)
{
    uint8_t *table_entry = dev->msix_table_page + vector * PCI_MSIX_ENTRY_SIZE;
    uint64_t address;
    uint32_t data;

    if (vector >= dev->msix_entries_nr || !dev->msix_entry_used[vector])
        return;
    if (msix_is_masked(dev, vector)) {
        msix_set_pending(dev, vector);
        return;
    }

    address = pci_get_quad(table_entry + PCI_MSIX_ENTRY_LOWER_ADDR);
    data = pci_get_long(table_entry + PCI_MSIX_ENTRY_DATA);
    stl_le_phys(address, data);
}

void msix_reset(PCIDevice *dev)
{
    if (!(dev->cap_present & QEMU_PCI_CAP_MSIX))
        return;
    msix_free_irq_entries(dev);
    dev->config[dev->msix_cap + MSIX_CONTROL_OFFSET] &=
	    ~dev->wmask[dev->msix_cap + MSIX_CONTROL_OFFSET];
    memset(dev->msix_table_page, 0, MSIX_PAGE_SIZE);
    msix_mask_all(dev, dev->msix_entries_nr);
}

/* PCI spec suggests that devices make it possible for software to configure
 * less vectors than supported by the device, but does not specify a standard
 * mechanism for devices to do so.
 *
 * We support this by asking devices to declare vectors software is going to
 * actually use, and checking this on the notification path. Devices that
 * don't want to follow the spec suggestion can declare all vectors as used. */

/* Mark vector as used. */
int msix_vector_use(PCIDevice *dev, unsigned vector)
{
    if (vector >= dev->msix_entries_nr)
        return -EINVAL;
    dev->msix_entry_used[vector]++;
    return 0;
}

/* Mark vector as unused. */
void msix_vector_unuse(PCIDevice *dev, unsigned vector)
{
    if (vector >= dev->msix_entries_nr || !dev->msix_entry_used[vector]) {
        return;
    }
    if (--dev->msix_entry_used[vector]) {
        return;
    }
    msix_clr_pending(dev, vector);
}

void msix_unuse_all_vectors(PCIDevice *dev)
{
    if (!(dev->cap_present & QEMU_PCI_CAP_MSIX))
        return;
    msix_free_irq_entries(dev);
}
