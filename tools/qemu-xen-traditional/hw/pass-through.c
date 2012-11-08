/*
 * Copyright (c) 2007, Neocleus Corporation.
 * Copyright (c) 2007, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Alex Novik <alex@neocleus.com>
 * Allen Kay <allen.m.kay@intel.com>
 * Guy Zana <guy@neocleus.com>
 *
 * This file implements direct PCI assignment to a HVM guest
 */

/*
 * Interrupt Disable policy:
 *
 * INTx interrupt:
 *   Initialize(register_real_device)
 *     Map INTx(xc_physdev_map_pirq):
 *       <fail>
 *         - Set real Interrupt Disable bit to '1'.
 *         - Set machine_irq and assigned_device->machine_irq to '0'.
 *         * Don't bind INTx.
 * 
 *     Bind INTx(xc_domain_bind_pt_pci_irq):
 *       <fail>
 *         - Set real Interrupt Disable bit to '1'.
 *         - Unmap INTx.
 *         - Decrement mapped_machine_irq[machine_irq]
 *         - Set assigned_device->machine_irq to '0'.
 * 
 *   Write to Interrupt Disable bit by guest software(pt_cmd_reg_write)
 *     Write '0'
 *       <ptdev->msi_trans_en is false>
 *         - Set real bit to '0' if assigned_device->machine_irq isn't '0'.
 * 
 *     Write '1'
 *       <ptdev->msi_trans_en is false>
 *         - Set real bit to '1'.
 * 
 * MSI-INTx translation.
 *   Initialize(xc_physdev_map_pirq_msi/pt_msi_setup)
 *     Bind MSI-INTx(xc_domain_bind_pt_irq)
 *       <fail>
 *         - Unmap MSI.
 *           <success>
 *             - Set dev->msi->pirq to '-1'.
 *           <fail>
 *             - Do nothing.
 * 
 *   Write to Interrupt Disable bit by guest software(pt_cmd_reg_write)
 *     Write '0'
 *       <ptdev->msi_trans_en is true>
 *         - Set MSI Enable bit to '1'.
 * 
 *     Write '1'
 *       <ptdev->msi_trans_en is true>
 *         - Set MSI Enable bit to '0'.
 * 
 * MSI interrupt:
 *   Initialize MSI register(pt_msi_setup, pt_msi_update)
 *     Bind MSI(xc_domain_update_msi_irq)
 *       <fail>
 *         - Unmap MSI.
 *         - Set dev->msi->pirq to '-1'.
 * 
 * MSI-X interrupt:
 *   Initialize MSI-X register(pt_msix_update_one)
 *     Bind MSI-X(xc_domain_update_msi_irq)
 *       <fail>
 *         - Unmap MSI-X.
 *         - Set entry->pirq to '-1'.
 */

#include "pass-through.h"
#include "pci/header.h"
#include "pci/pci.h"
#include "pt-msi.h"
#include "qemu-xen.h"
#include "iomulti.h"

#include <unistd.h>
#include <sys/ioctl.h>
#include <assert.h>

extern int gfx_passthru;
int igd_passthru = 0;

struct php_dev {
    struct pt_dev *pt_dev;
    uint8_t valid;
    uint8_t r_bus;
    uint8_t r_dev;
    uint8_t r_func;
    char *opt;
};
struct dpci_infos {

    struct php_dev php_devs[NR_PCI_DEVFN];

    PCIBus *e_bus;
    struct pci_access *pci_access;

} dpci_infos;

char mapped_machine_irq[PT_NR_IRQS] = {0};

/* prototype */
static uint32_t pt_common_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_vendor_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_device_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_ptr_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_status_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_irqpin_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_bar_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_pmc_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_pmcsr_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_linkctrl_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_devctrl2_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_linkctrl2_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_msgctrl_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_msgaddr64_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_msgdata_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_msixctrl_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint32_t pt_header_type_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset);
static uint8_t pt_reg_grp_size_init(struct pt_dev *ptdev,
    struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset);
static uint8_t pt_pm_size_init(struct pt_dev *ptdev,
    struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset);
static uint8_t pt_msi_size_init(struct pt_dev *ptdev,
    struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset);
static uint8_t pt_msix_size_init(struct pt_dev *ptdev,
    struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset);
static uint8_t pt_vendor_size_init(struct pt_dev *ptdev,
    struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset);
static uint8_t pt_pcie_size_init(struct pt_dev *ptdev,
    struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset);
static int pt_byte_reg_read(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint8_t *valueu, uint8_t valid_mask);
static int pt_word_reg_read(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t valid_mask);
static int pt_long_reg_read(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t *value, uint32_t valid_mask);
static int pt_cmd_reg_read(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t valid_mask);
static int pt_bar_reg_read(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t *value, uint32_t valid_mask);
static int pt_pmcsr_reg_read(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t valid_mask);
static int pt_byte_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint8_t *value, uint8_t dev_value, uint8_t valid_mask);
static int pt_word_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t dev_value, uint16_t valid_mask);
static int pt_long_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t *value, uint32_t dev_value, uint32_t valid_mask);
static int pt_cmd_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t dev_value, uint16_t valid_mask);
static int pt_bar_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t *value, uint32_t dev_value, uint32_t valid_mask);
static int pt_exp_rom_bar_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t *value, uint32_t dev_value, uint32_t valid_mask);
static int pt_pmcsr_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t dev_value, uint16_t valid_mask);
static int pt_msgctrl_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t dev_value, uint16_t valid_mask);
static int pt_msgaddr32_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t *value, uint32_t dev_value, uint32_t valid_mask);
static int pt_msgaddr64_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t *value, uint32_t dev_value, uint32_t valid_mask);
static int pt_msgdata_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t dev_value, uint16_t valid_mask);
static int pt_msixctrl_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t dev_value, uint16_t valid_mask);
static int pt_byte_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint8_t dev_value, uint8_t *value);
static int pt_word_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint16_t dev_value, uint16_t *value);
static int pt_long_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint32_t dev_value, uint32_t *value);
static int pt_cmd_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint16_t dev_value, uint16_t *value);
static int pt_pmcsr_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint16_t dev_value, uint16_t *value);
static int pt_bar_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint32_t dev_value, uint32_t *value);
static int pt_exp_rom_bar_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint32_t dev_value, uint32_t *value);
static int pt_intel_opregion_read(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t *value, uint32_t valid_mask);
static int pt_intel_opregion_write(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t *value, uint32_t dev_value, uint32_t valid_mask);
static uint8_t pt_reg_grp_header0_size_init(struct pt_dev *ptdev,
        struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset);

/* pt_reg_info_tbl declaration
 * - only for emulated register (either a part or whole bit).
 * - for passthrough register that need special behavior (like interacting with
 *   other component), set emu_mask to all 0 and specify r/w func properly.
 * - do NOT use ALL F for init_val, otherwise the tbl will not be registered.
 */

/* Header Type0 reg static infomation table */
static struct pt_reg_info_tbl pt_emu_reg_header0_tbl[] = {
    /* Vendor ID reg */
    {
        .offset     = PCI_VENDOR_ID,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xFFFF,
        .emu_mask   = 0xFFFF,
        .init       = pt_vendor_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_word_reg_write,
        .u.w.restore  = NULL,
    },
    /* Device ID reg */
    {
        .offset     = PCI_DEVICE_ID,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xFFFF,
        .emu_mask   = 0xFFFF,
        .init       = pt_device_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_word_reg_write,
        .u.w.restore  = NULL,
    },
    /* Command reg */
    {
        .offset     = PCI_COMMAND,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xF880,
        .emu_mask   = 0x0740,
        .init       = pt_common_reg_init,
        .u.w.read   = pt_cmd_reg_read,
        .u.w.write  = pt_cmd_reg_write,
        .u.w.restore  = pt_cmd_reg_restore,
    },
    /* Capabilities Pointer reg */
    {
        .offset     = PCI_CAPABILITY_LIST,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = pt_ptr_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = NULL,
    },
    /* Status reg */
    /* use emulated Cap Ptr value to initialize,
     * so need to be declared after Cap Ptr reg
     */
    {
        .offset     = PCI_STATUS,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0x06FF,
        .emu_mask   = 0x0010,
        .init       = pt_status_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_word_reg_write,
        .u.w.restore  = NULL,
    },
    /* Cache Line Size reg */
    {
        .offset     = PCI_CACHE_LINE_SIZE,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0x00,
        .emu_mask   = 0xFF,
        .init       = pt_common_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = pt_byte_reg_restore,
    },
    /* Latency Timer reg */
    {
        .offset     = PCI_LATENCY_TIMER,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0x00,
        .emu_mask   = 0xFF,
        .init       = pt_common_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = pt_byte_reg_restore,
    },
    /* Header Type reg */
    {
        .offset     = PCI_HEADER_TYPE,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0x00,
        .init       = pt_header_type_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = NULL,
    },
    /* Interrupt Line reg */
    {
        .offset     = PCI_INTERRUPT_LINE,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0x00,
        .emu_mask   = 0xFF,
        .init       = pt_common_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = NULL,
    },
    /* Interrupt Pin reg */
    {
        .offset     = PCI_INTERRUPT_PIN,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = pt_irqpin_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = NULL,
    },
    /* BAR 0 reg */
    /* mask of BAR need to be decided later, depends on IO/MEM type */
    {
        .offset     = PCI_BASE_ADDRESS_0,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = pt_bar_reg_init,
        .u.dw.read  = pt_bar_reg_read,
        .u.dw.write = pt_bar_reg_write,
        .u.dw.restore = pt_bar_reg_restore,
    },
    /* BAR 1 reg */
    {
        .offset     = PCI_BASE_ADDRESS_1,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = pt_bar_reg_init,
        .u.dw.read  = pt_bar_reg_read,
        .u.dw.write = pt_bar_reg_write,
        .u.dw.restore = pt_bar_reg_restore,
    },
    /* BAR 2 reg */
    {
        .offset     = PCI_BASE_ADDRESS_2,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = pt_bar_reg_init,
        .u.dw.read  = pt_bar_reg_read,
        .u.dw.write = pt_bar_reg_write,
        .u.dw.restore = pt_bar_reg_restore,
    },
    /* BAR 3 reg */
    {
        .offset     = PCI_BASE_ADDRESS_3,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = pt_bar_reg_init,
        .u.dw.read  = pt_bar_reg_read,
        .u.dw.write = pt_bar_reg_write,
        .u.dw.restore = pt_bar_reg_restore,
    },
    /* BAR 4 reg */
    {
        .offset     = PCI_BASE_ADDRESS_4,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = pt_bar_reg_init,
        .u.dw.read  = pt_bar_reg_read,
        .u.dw.write = pt_bar_reg_write,
        .u.dw.restore = pt_bar_reg_restore,
    },
    /* BAR 5 reg */
    {
        .offset     = PCI_BASE_ADDRESS_5,
        .size       = 4,
        .init_val   = 0x00000000,
        .init       = pt_bar_reg_init,
        .u.dw.read  = pt_bar_reg_read,
        .u.dw.write = pt_bar_reg_write,
        .u.dw.restore = pt_bar_reg_restore,
    },
    /* Expansion ROM BAR reg */
    {
        .offset     = PCI_ROM_ADDRESS,
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = 0x000007FE,
        .emu_mask   = 0xFFFFF800,
        .init       = pt_bar_reg_init,
        .u.dw.read  = pt_long_reg_read,
        .u.dw.write = pt_exp_rom_bar_reg_write,
        .u.dw.restore = pt_exp_rom_bar_reg_restore,
    },
    /* Intel IGFX OpRegion reg */
    {
        .offset     = PCI_INTEL_OPREGION,
        .size       = 4,
        .init_val   = 0,
        .no_wb      = 1,
        .u.dw.read   = pt_intel_opregion_read,
        .u.dw.write  = pt_intel_opregion_write,
        .u.dw.restore  = NULL,
    },
    {
        .size = 0,
    },
};

/* Power Management Capability reg static infomation table */
static struct pt_reg_info_tbl pt_emu_reg_pm_tbl[] = {
    /* Next Pointer reg */
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = pt_ptr_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = NULL,
    },
    /* Power Management Capabilities reg */
    {
        .offset     = PCI_CAP_FLAGS,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xFFFF,
        .emu_mask   = 0xF9C8,
        .init       = pt_pmc_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_word_reg_write,
        .u.w.restore  = NULL,
    },
    /* PCI Power Management Control/Status reg */
    {
        .offset     = PCI_PM_CTRL,
        .size       = 2,
        .init_val   = 0x0008,
        .ro_mask    = 0xE1FC,
        .emu_mask   = 0x8100,
        .init       = pt_pmcsr_reg_init,
        .u.w.read   = pt_pmcsr_reg_read,
        .u.w.write  = pt_pmcsr_reg_write,
        .u.w.restore  = pt_pmcsr_reg_restore,
    },
    {
        .size = 0,
    },
};

/* Vital Product Data Capability Structure reg static infomation table */
static struct pt_reg_info_tbl pt_emu_reg_vpd_tbl[] = {
    /* Next Pointer reg */
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = pt_ptr_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = NULL,
    },
    {
        .size = 0,
    },
};

/* Vendor Specific Capability Structure reg static infomation table */
static struct pt_reg_info_tbl pt_emu_reg_vendor_tbl[] = {
    /* Next Pointer reg */
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = pt_ptr_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = NULL,
    },
    {
        .size = 0,
    },
};

/* PCI Express Capability Structure reg static infomation table */
static struct pt_reg_info_tbl pt_emu_reg_pcie_tbl[] = {
    /* Next Pointer reg */
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = pt_ptr_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = NULL,
    },
    /* Device Capabilities reg */
    {
        .offset     = PCI_EXP_DEVCAP,
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = 0x1FFCFFFF,
        .emu_mask   = 0x10000000,
        .init       = pt_common_reg_init,
        .u.dw.read  = pt_long_reg_read,
        .u.dw.write = pt_long_reg_write,
        .u.dw.restore = NULL,
    },
    /* Device Control reg */
    {
        .offset     = PCI_EXP_DEVCTL,
        .size       = 2,
        .init_val   = 0x2810,
        .ro_mask    = 0x8400,
        .emu_mask   = 0xFFFF,
        .init       = pt_common_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_word_reg_write,
        .u.w.restore  = pt_word_reg_restore,
    },
    /* Link Control reg */
    {
        .offset     = PCI_EXP_LNKCTL,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xFC34,
        .emu_mask   = 0xFFFF,
        .init       = pt_linkctrl_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_word_reg_write,
        .u.w.restore  = pt_word_reg_restore,
    },
    /* Device Control 2 reg */
    {
        .offset     = 0x28,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xFFE0,
        .emu_mask   = 0xFFFF,
        .init       = pt_devctrl2_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_word_reg_write,
        .u.w.restore  = pt_word_reg_restore,
    },
    /* Link Control 2 reg */
    {
        .offset     = 0x30,
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xE040,
        .emu_mask   = 0xFFFF,
        .init       = pt_linkctrl2_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_word_reg_write,
        .u.w.restore  = pt_word_reg_restore,
    },
    {
        .size = 0,
    },
};

/* MSI Capability Structure reg static infomation table */
static struct pt_reg_info_tbl pt_emu_reg_msi_tbl[] = {
    /* Next Pointer reg */
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = pt_ptr_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = NULL,
    },
    /* Message Control reg */
    {
        .offset     = PCI_MSI_FLAGS, // 2
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0xFF8E,
        .emu_mask   = 0x007F,
        .init       = pt_msgctrl_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_msgctrl_reg_write,
        .u.w.restore  = NULL,
    },
    /* Message Address reg */
    {
        .offset     = PCI_MSI_ADDRESS_LO, // 4
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = 0x00000003,
        .emu_mask   = 0xFFFFFFFF,
        .no_wb      = 1,
        .init       = pt_common_reg_init,
        .u.dw.read  = pt_long_reg_read,
        .u.dw.write = pt_msgaddr32_reg_write,
        .u.dw.restore = NULL,
    },
    /* Message Upper Address reg (if PCI_MSI_FLAGS_64BIT set) */
    {
        .offset     = PCI_MSI_ADDRESS_HI, // 8
        .size       = 4,
        .init_val   = 0x00000000,
        .ro_mask    = 0x00000000,
        .emu_mask   = 0xFFFFFFFF,
        .no_wb      = 1,
        .init       = pt_msgaddr64_reg_init,
        .u.dw.read  = pt_long_reg_read,
        .u.dw.write = pt_msgaddr64_reg_write,
        .u.dw.restore = NULL,
    },
    /* Message Data reg (16 bits of data for 32-bit devices) */
    {
        .offset     = PCI_MSI_DATA_32, // 8
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0x0000,
        .emu_mask   = 0xFFFF,
        .no_wb      = 1,
        .init       = pt_msgdata_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_msgdata_reg_write,
        .u.w.restore  = NULL,
    },
    /* Message Data reg (16 bits of data for 64-bit devices) */
    {
        .offset     = PCI_MSI_DATA_64, // 12
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0x0000,
        .emu_mask   = 0xFFFF,
        .no_wb      = 1,
        .init       = pt_msgdata_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_msgdata_reg_write,
        .u.w.restore  = NULL,
    },
    {
        .size = 0,
    },
};

/* MSI-X Capability Structure reg static infomation table */
static struct pt_reg_info_tbl pt_emu_reg_msix_tbl[] = {
    /* Next Pointer reg */
    {
        .offset     = PCI_CAP_LIST_NEXT,
        .size       = 1,
        .init_val   = 0x00,
        .ro_mask    = 0xFF,
        .emu_mask   = 0xFF,
        .init       = pt_ptr_reg_init,
        .u.b.read   = pt_byte_reg_read,
        .u.b.write  = pt_byte_reg_write,
        .u.b.restore  = NULL,
    },
    /* Message Control reg */
    {
        .offset     = PCI_MSI_FLAGS, // 2
        .size       = 2,
        .init_val   = 0x0000,
        .ro_mask    = 0x3FFF,
        .emu_mask   = 0x0000,
        .init       = pt_msixctrl_reg_init,
        .u.w.read   = pt_word_reg_read,
        .u.w.write  = pt_msixctrl_reg_write,
        .u.w.restore  = NULL,
    },
    {
        .size = 0,
    },
};

/* pt_reg_grp_info_tbl declaration
 * - only for emulated or zero-hardwired register group.
 * - for register group with dynamic size, just set grp_size to 0xFF and
 *   specify size_init func properly.
 * - no need to specify emu_reg_tbl for zero-hardwired type.
 */

/* emul reg group static infomation table */
static const struct pt_reg_grp_info_tbl pt_emu_reg_grp_tbl[] = {
    /* Header Type0 reg group */
    {
        .grp_id     = 0xFF,
        .grp_type   = GRP_TYPE_EMU,
        .grp_size   = 0x40,
        .size_init  = pt_reg_grp_header0_size_init,
        .emu_reg_tbl= pt_emu_reg_header0_tbl,
    },
    /* PCI PowerManagement Capability reg group */
    {
        .grp_id     = PCI_CAP_ID_PM,
        .grp_type   = GRP_TYPE_EMU,
        .grp_size   = PCI_PM_SIZEOF,
        .size_init  = pt_pm_size_init,
        .emu_reg_tbl= pt_emu_reg_pm_tbl,
    },
    /* AGP Capability Structure reg group */
    {
        .grp_id     = PCI_CAP_ID_AGP,
        .grp_type   = GRP_TYPE_HARDWIRED,
        .grp_size   = 0x30,
        .size_init  = pt_reg_grp_size_init,
    },
    /* Vital Product Data Capability Structure reg group */
    {
        .grp_id     = PCI_CAP_ID_VPD,
        .grp_type   = GRP_TYPE_EMU,
        .grp_size   = 0x08,
        .size_init  = pt_reg_grp_size_init,
        .emu_reg_tbl= pt_emu_reg_vpd_tbl,
    },
    /* Slot Identification reg group */
    {
        .grp_id     = PCI_CAP_ID_SLOTID,
        .grp_type   = GRP_TYPE_HARDWIRED,
        .grp_size   = 0x04,
        .size_init  = pt_reg_grp_size_init,
    },
#ifndef __ia64__
    /* At present IA64 Xen doesn't support MSI for passthrough, so let's not
     * expose MSI capability to IA64 HVM guest for now.
     */
    /* MSI Capability Structure reg group */
    {
        .grp_id     = PCI_CAP_ID_MSI,
        .grp_type   = GRP_TYPE_EMU,
        .grp_size   = 0xFF,
        .size_init  = pt_msi_size_init,
        .emu_reg_tbl= pt_emu_reg_msi_tbl,
    },
#endif
    /* PCI-X Capabilities List Item reg group */
    {
        .grp_id     = PCI_CAP_ID_PCIX,
        .grp_type   = GRP_TYPE_HARDWIRED,
        .grp_size   = 0x18,
        .size_init  = pt_reg_grp_size_init,
    },
    /* Vendor Specific Capability Structure reg group */
    {
        .grp_id     = PCI_CAP_ID_VNDR,
        .grp_type   = GRP_TYPE_EMU,
        .grp_size   = 0xFF,
        .size_init  = pt_vendor_size_init,
        .emu_reg_tbl= pt_emu_reg_vendor_tbl,
    },
    /* SHPC Capability List Item reg group */
    {
        .grp_id     = PCI_CAP_ID_HOTPLUG,
        .grp_type   = GRP_TYPE_HARDWIRED,
        .grp_size   = 0x08,
        .size_init  = pt_reg_grp_size_init,
    },
    /* Subsystem ID and Subsystem Vendor ID Capability List Item reg group */
    {
        .grp_id     = PCI_CAP_ID_SSVID,
        .grp_type   = GRP_TYPE_HARDWIRED,
        .grp_size   = 0x08,
        .size_init  = pt_reg_grp_size_init,
    },
    /* AGP 8x Capability Structure reg group */
    {
        .grp_id     = PCI_CAP_ID_AGP3,
        .grp_type   = GRP_TYPE_HARDWIRED,
        .grp_size   = 0x30,
        .size_init  = pt_reg_grp_size_init,
    },
    /* PCI Express Capability Structure reg group */
    {
        .grp_id     = PCI_CAP_ID_EXP,
        .grp_type   = GRP_TYPE_EMU,
        .grp_size   = 0xFF,
        .size_init  = pt_pcie_size_init,
        .emu_reg_tbl= pt_emu_reg_pcie_tbl,
    },
#ifndef __ia64__
    /* At present IA64 Xen doesn't support MSI for passthrough, so let's not
     * expose MSI-X capability to IA64 HVM guest for now.
     */
    /* MSI-X Capability Structure reg group */
    {
        .grp_id     = PCI_CAP_ID_MSIX,
        .grp_type   = GRP_TYPE_EMU,
        .grp_size   = 0x0C,
        .size_init  = pt_msix_size_init,
        .emu_reg_tbl= pt_emu_reg_msix_tbl,
    },
#endif
    {
        .grp_size = 0,
    },
};

static int token_value(char *token)
{
    return strtol(token, NULL, 16);
}

static int parse_bdf(char **str, int *seg, int *bus, int *dev, int *func,
                     char **opt, int *vdevfn)
{
    char *token, *endptr;
    const char *delim = ":.";

    if ( !(*str) ||
          ( !strchr(*str, ':') && !strchr(*str, '.')) )
        return 0;

    token  = strsep(str, delim);
    *seg = token_value(token);

    token  = strsep(str, delim);
    *bus  = token_value(token);

    token  = strsep(str, delim);
    *dev  = token_value(token);

    token  = strsep(str, delim);

    *opt = strchr(token, '@');
    if (*opt)
    {
        *(*opt)++ = '\0';
        *vdevfn = token_value(*opt);
    }
    else
    {
        *vdevfn = AUTO_PHP_SLOT;
        *opt = token;
    }

    *opt = strchr(*opt, ',');
    if (*opt)
        *(*opt)++ = '\0';

    *func  = token_value(token);

    return 1;
}

static int get_next_keyval(char **option, char **key, char **val)
{
    char *opt, *k, *v;

    k = *option;
    opt = strchr(k, ',');
    if (opt)
        *opt++ = '\0';
    v = strchr(k, '=');
    if (!v)
        return -1;
    *v++ = '\0';

    *key = k;
    *val = v;
    *option = opt;

    return 0;
}

static int pci_devfn_match(int bus, int dev, int func, int devfn)
{
    if (test_pci_devfn(devfn) == 1 &&
        dpci_infos.php_devs[devfn].r_bus == bus &&
        dpci_infos.php_devs[devfn].r_dev  == dev &&
        dpci_infos.php_devs[devfn].r_func == func )
        return 1;
    return 0;
}

static int find_free_vslot(void)
{
    PCIBus *e_bus = dpci_infos.e_bus;
    int slot, func, devfn;

    for ( slot = 0; slot < NR_PCI_DEV; slot++ )
    {
        if ( gfx_passthru && slot == 0x2 )
            continue;
        for ( func = 0; func < NR_PCI_FUNC; func++ )
        {
            devfn = PCI_DEVFN(slot, func);
            if ( test_pci_devfn(devfn) || pci_devfn_in_use(e_bus, devfn) )
            {
                break;
            }
        }
        if (func == NR_PCI_FUNC)
            return slot;
    }

    /* not found */
    return -1;
}


/* Insert a new pass-through device into a specific pci devfn.
 * input  dom:bus:dev.func@devfn, chose free one if devfn & AUTO_PHP_SLOT
 * return -2: requested devfn not available
 *        -1: no free devfns
 *        >=0: the new hotplug devfn
 */
static int __insert_to_pci_devfn(int bus, int dev, int func, int devfn,
                                 char *opt)
{
    PCIBus *e_bus = dpci_infos.e_bus;
    int vslot;

    if ( gfx_passthru && bus == 0x0 && dev == 0x2 )
    {
        igd_passthru = 1;

        /* make virtual BDF of Intel IGD in guest is same with host */
        devfn = PCI_DEVFN(dev, func);
        if ( test_pci_devfn(devfn) || pci_devfn_in_use(e_bus, devfn) )
            return -2;
    }
    else if ( devfn & AUTO_PHP_SLOT )
    {
        vslot = find_free_vslot();
        if (vslot < 0)
            return -1;
        /* The vfunc is provided in the devfn paramter */
        devfn = PCI_DEVFN(vslot, PCI_FUNC(devfn));
    }
    else
    {
        /* Prefered devfn */
        if ( test_pci_devfn(devfn) || pci_devfn_in_use(e_bus, devfn) )
            return -2;
    }

    dpci_infos.php_devs[devfn].valid  = 1;
    dpci_infos.php_devs[devfn].r_bus  = bus;
    dpci_infos.php_devs[devfn].r_dev  = dev;
    dpci_infos.php_devs[devfn].r_func = func;
    dpci_infos.php_devs[devfn].opt = opt;
    return devfn;
}

/* Insert a new pass-through device into a specific pci devfn.
 * input  dom:bus:dev.func@devfn
 */
int insert_to_pci_devfn(char *bdf_slt)
{
    int seg, bus, dev, func, devfn;
    char *opt;

    if ( !parse_bdf(&bdf_slt, &seg, &bus, &dev, &func, &opt, &devfn) )
    {
        return -1;
    }

    return __insert_to_pci_devfn(bus, dev, func, devfn, opt);

}

/* Test if a pci devfn has a PHP device
 * 1:  present
 * 0:  not present
 * -1: invalid pci devfn input
 */
int test_pci_devfn(int devfn)
{
    if ( devfn < 0 || devfn >= NR_PCI_DEVFN )
        return -1;

    if ( dpci_infos.php_devs[devfn].valid )
        return 1;

    return 0;
}

/* find the pci devfn for pass-through dev with specified BDF */
int bdf_to_devfn(char *bdf_str)
{
    int seg, bus, dev, func, devfn, i;
    char *opt;

    if ( !parse_bdf(&bdf_str, &seg, &bus, &dev, &func, &opt, &devfn))
    {
        return -1;
    }

    /* locate the virtual pci devfn for this VTd device */
    for ( i = 0; i < NR_PCI_DEVFN; i++ )
    {
        if ( pci_devfn_match(bus, dev, func, i) )
            return i;
    }

    return -1;
}

static uint8_t pci_read_intx(struct pt_dev *ptdev)
{
    return pci_read_byte(ptdev->pci_dev, PCI_INTERRUPT_PIN);
}

/* The PCI Local Bus Specification, Rev. 3.0,
 * Section 6.2.4 Miscellaneous Registers, pp 223
 * outlines 5 valid values for the intertupt pin (intx).
 *  0: For devices (or device functions) that don't use an interrupt in
 *  1: INTA#
 *  2: INTB#
 *  3: INTC#
 *  4: INTD#
 *
 * Xen uses the following 4 values for intx
 *  0: INTA#
 *  1: INTB#
 *  2: INTC#
 *  3: INTD#
 *
 * Observing that these list of values are not the same, pci_read_intx()
 * uses the following mapping from hw to xen values.
 * This seems to reflect the current usage within Xen.
 *
 * PCI hardware    | Xen | Notes
 * ----------------+-----+----------------------------------------------------
 * 0               | 0   | No interrupt
 * 1               | 0   | INTA#
 * 2               | 1   | INTB#
 * 3               | 2   | INTC#
 * 4               | 3   | INTD#
 * any other value | 0   | This should never happen, log error message
 */
uint8_t pci_intx(struct pt_dev *ptdev)
{
    uint8_t r_val = pci_read_intx(ptdev);

    PT_LOG("intx=%i\n", r_val);
    if (r_val < 1 || r_val > 4)
    {
        PT_LOG("Interrupt pin read from hardware is out of range: "
               "value=%i, acceptable range is 1 - 4\n", r_val);
        r_val = 0;
    }
    else
    {
        r_val -= 1;
    }

    return r_val;
}

static int _pt_iomem_helper(struct pt_dev *assigned_device, int i,
                            uint32_t e_base, uint32_t e_size, int op)
{
    if ( has_msix_mapping(assigned_device, i) )
    {
        uint32_t msix_last_pfn = (assigned_device->msix->mmio_base_addr - 1 +
            assigned_device->msix->total_entries * 16) >> XC_PAGE_SHIFT;
        uint32_t bar_last_pfn = (e_base + e_size - 1) >> XC_PAGE_SHIFT;
        int ret = 0;

        if ( assigned_device->msix->table_off )
            ret = xc_domain_memory_mapping(xc_handle, domid,
                e_base >> XC_PAGE_SHIFT,
                assigned_device->bases[i].access.maddr >> XC_PAGE_SHIFT,
                (assigned_device->msix->mmio_base_addr >> XC_PAGE_SHIFT)
                - (e_base >> XC_PAGE_SHIFT), op);

        if ( ret == 0 && msix_last_pfn != bar_last_pfn )
        {
            assert(msix_last_pfn < bar_last_pfn);
            ret = xc_domain_memory_mapping(xc_handle, domid,
                msix_last_pfn + 1,
                (assigned_device->bases[i].access.maddr +
                 assigned_device->msix->table_off +
                 assigned_device->msix->total_entries * 16 +
                 XC_PAGE_SIZE - 1) >> XC_PAGE_SHIFT,
                bar_last_pfn - msix_last_pfn, op);
        }

        return ret;
    }

    return xc_domain_memory_mapping(xc_handle, domid,
        e_base >> XC_PAGE_SHIFT,
        assigned_device->bases[i].access.maddr >> XC_PAGE_SHIFT,
        (e_size + XC_PAGE_SIZE - 1) >> XC_PAGE_SHIFT, op);
}

/* Being called each time a mmio region has been updated */
static void pt_iomem_map(PCIDevice *d, int i, uint32_t e_phys, uint32_t e_size,
                         int type)
{
    struct pt_dev *assigned_device  = (struct pt_dev *)d;
    uint32_t old_ebase = assigned_device->bases[i].e_physbase;
    int first_map = ( assigned_device->bases[i].e_size == 0 );
    int ret = 0;

    assigned_device->bases[i].e_physbase = e_phys;
    assigned_device->bases[i].e_size= e_size;

    PT_LOG("e_phys=%08x maddr=%lx type=%d len=%d index=%d first_map=%d\n",
        e_phys, (unsigned long)assigned_device->bases[i].access.maddr,
        type, e_size, i, first_map);

    if ( e_size == 0 )
        return;

    if ( !first_map && old_ebase != -1 )
    {
        if ( has_msix_mapping(assigned_device, i) )
            unregister_iomem(assigned_device->msix->mmio_base_addr);

        ret = _pt_iomem_helper(assigned_device, i, old_ebase, e_size,
                               DPCI_REMOVE_MAPPING);
        if ( ret != 0 )
        {
            PT_LOG("Error: remove old mapping failed!\n");
            return;
        }
    }

    /* map only valid guest address */
    if (e_phys != -1)
    {
        if ( has_msix_mapping(assigned_device, i) )
        {
            assigned_device->msix->mmio_base_addr =
                assigned_device->bases[i].e_physbase
                + assigned_device->msix->table_off;

            cpu_register_physical_memory(assigned_device->msix->mmio_base_addr,
                 (assigned_device->msix->total_entries * 16 + XC_PAGE_SIZE - 1)
                  & XC_PAGE_MASK,
                 assigned_device->msix->mmio_index);
        }

        ret = _pt_iomem_helper(assigned_device, i, e_phys, e_size,
                               DPCI_ADD_MAPPING);
        if ( ret != 0 )
        {
            PT_LOG("Error: create new mapping failed!\n");
            return;
        }

        if ( old_ebase != e_phys && old_ebase != -1 )
            pt_msix_update_remap(assigned_device, i);
    }
}

#ifndef CONFIG_STUBDOM

#define PCI_IOMUL_DEV_PATH      "/dev/xen/pci_iomul"
static void pt_iomul_init(struct pt_dev *assigned_device,
                          uint8_t r_bus, uint8_t r_dev, uint8_t r_func)
{
    int fd = PCI_IOMUL_INVALID_FD;
    struct pci_iomul_setup setup = {
        .segment = 0,
        .bus = r_bus,
        .dev = r_dev,
        .func = r_func,
    };

    fd = open(PCI_IOMUL_DEV_PATH, O_RDWR);
    if ( fd < 0 ) {
        PT_LOG("Error: %s can't open file %s: %s: 0x%x:0x%x.0x%x\n",
               __func__, PCI_IOMUL_DEV_PATH, strerror(errno),
               r_bus, r_dev, r_func);
        fd = PCI_IOMUL_INVALID_FD;
    }

    if ( fd >= 0 && ioctl(fd, PCI_IOMUL_SETUP, &setup) )
    {
        PT_LOG("Error: %s: %s: setup io multiplexing failed! 0x%x:0x%x.0x%x\n",
               __func__, strerror(errno), r_bus, r_dev, r_func);
        close(fd);
        fd = PCI_IOMUL_INVALID_FD;
    }

    assigned_device->fd = fd;
    if (fd != PCI_IOMUL_INVALID_FD)
        PT_LOG("io mul: 0x%x:0x%x.0x%x\n", r_bus, r_dev, r_func);
}

static void pt_iomul_free(struct pt_dev *assigned_device)
{
    if ( !pt_is_iomul(assigned_device) )
        return;

    close(assigned_device->fd);
    assigned_device->fd = PCI_IOMUL_INVALID_FD;
}

static void pt_iomul_get_bar_offset(struct pt_dev *assigned_device,
                                    uint32_t addr,
                                    uint8_t *bar, uint64_t *offset)
{
    for ( *bar = 0; *bar < PCI_BAR_ENTRIES; (*bar)++ )
    {
        const struct pt_region* r = &assigned_device->bases[*bar];
        if ( r->bar_flag != PT_BAR_FLAG_IO )
            continue;

        if ( r->e_physbase <= addr && addr < r->e_physbase + r->e_size )
        {
            *offset = addr - r->e_physbase;
            return;
        }
    }
}

static void pt_iomul_ioport_write(struct pt_dev *assigned_device,
                                  uint32_t addr, uint32_t val, int size)
{
    uint8_t bar;
    uint64_t offset;
    struct pci_iomul_out out;

    if ( !assigned_device->io_enable )
        return;

    pt_iomul_get_bar_offset(assigned_device, addr, &bar, &offset);
    if ( bar >= PCI_BAR_ENTRIES )
    {
        PT_LOG("error: %s: addr 0x%x val 0x%x size %d\n",
               __func__, addr, val, size);
        return;
    }

    out.bar = bar;
    out.offset = offset;
    out.size = size;
    out.value = val;
    if ( ioctl(assigned_device->fd, PCI_IOMUL_OUT, &out) )
        PT_LOG("error: %s: %s addr 0x%x size %d bar %d offset 0x%"PRIx64"\n",
               __func__, strerror(errno), addr, size, bar, offset);
}

static uint32_t pt_iomul_ioport_read(struct pt_dev *assigned_device,
                                     uint32_t addr, int size)
{
    uint8_t bar;
    uint64_t offset;
    struct pci_iomul_in in;

    if ( !assigned_device->io_enable )
        return -1;

    pt_iomul_get_bar_offset(assigned_device, addr, &bar, &offset);
    if ( bar >= PCI_BAR_ENTRIES )
    {
        PT_LOG("error: %s: addr 0x%x size %d\n", __func__, addr, size);
        return -1;
    }

    in.bar = bar;
    in.offset = offset;
    in.size = size;
    if ( ioctl(assigned_device->fd, PCI_IOMUL_IN, &in) )
    {
        PT_LOG("error: %s: %s addr 0x%x size %d bar %d offset 0x%"PRIx64"\n",
               __func__, strerror(errno), addr, size, bar, offset);
        in.value = -1;
    }

    return in.value;
}

#define DEFINE_PT_IOMUL_WRITE(size)                                     \
    static void pt_iomul_ioport_write ## size                           \
    (void *opaque, uint32_t addr, uint32_t val)                         \
    {                                                                   \
        pt_iomul_ioport_write((struct pt_dev *)opaque, addr, val,       \
                              (size));                                  \
    }

DEFINE_PT_IOMUL_WRITE(1)
DEFINE_PT_IOMUL_WRITE(2)
DEFINE_PT_IOMUL_WRITE(4)

#define DEFINE_PT_IOMUL_READ(size)                                      \
    static uint32_t pt_iomul_ioport_read ## size                        \
    (void *opaque, uint32_t addr)                                       \
    {                                                                   \
        return pt_iomul_ioport_read((struct pt_dev *)opaque, addr,      \
                                    (size));                            \
    }

DEFINE_PT_IOMUL_READ(1)
DEFINE_PT_IOMUL_READ(2)
DEFINE_PT_IOMUL_READ(4)

static void pt_iomul_ioport_map(struct pt_dev *assigned_device,
                                uint32_t old_ebase, uint32_t e_phys,
                                uint32_t e_size, int first_map)
{
    /* map only valid guest address (include 0) */
    if (e_phys != -1)
    {
        /* Create new mapping */
        register_ioport_write(e_phys, e_size, 1,
                              pt_iomul_ioport_write1, assigned_device);
        register_ioport_write(e_phys, e_size, 2,
                              pt_iomul_ioport_write2, assigned_device);
        register_ioport_write(e_phys, e_size, 4,
                              pt_iomul_ioport_write4, assigned_device);
        register_ioport_read(e_phys, e_size, 1,
                             pt_iomul_ioport_read1, assigned_device);
        register_ioport_read(e_phys, e_size, 2,
                             pt_iomul_ioport_read2, assigned_device);
        register_ioport_read(e_phys, e_size, 4,
                             pt_iomul_ioport_read4, assigned_device);
    }
}

#else /* CONFIG_STUBDOM */

static void pt_iomul_init(struct pt_dev *assigned_device,
                          uint8_t r_bus, uint8_t r_dev, uint8_t r_func) {
    fprintf(stderr, "warning: pt_iomul not supported in stubdom"
	    " %02x:%02x.%x\n", r_bus,r_dev,r_func);
}
static void pt_iomul_ioport_map(struct pt_dev *assigned_device,
                                uint32_t old_ebase, uint32_t e_phys,
				uint32_t e_size, int first_map) { abort(); }
static void pt_iomul_free(struct pt_dev *assigned_device) { }

#endif /* !CONFIG_STUBDOM */

/* Being called each time a pio region has been updated */
static void pt_ioport_map(PCIDevice *d, int i,
                          uint32_t e_phys, uint32_t e_size, int type)
{
    struct pt_dev *assigned_device  = (struct pt_dev *)d;
    uint32_t old_ebase = assigned_device->bases[i].e_physbase;
    int first_map = ( assigned_device->bases[i].e_size == 0 );
    int ret = 0;

    assigned_device->bases[i].e_physbase = e_phys;
    assigned_device->bases[i].e_size= e_size;

    PT_LOG("e_phys=%04x pio_base=%04x len=%d index=%d first_map=%d\n",
        (uint16_t)e_phys, (uint16_t)assigned_device->bases[i].access.pio_base,
        (uint16_t)e_size, i, first_map);

    if ( e_size == 0 )
        return;

    if ( pt_is_iomul(assigned_device) )
    {
        pt_iomul_ioport_map(assigned_device,
                            old_ebase, e_phys, e_size, first_map);
        return;
    }

    if ( !first_map && old_ebase != -1 )
    {
        /* Remove old mapping */
        ret = xc_domain_ioport_mapping(xc_handle, domid, old_ebase,
                    assigned_device->bases[i].access.pio_base, e_size,
                    DPCI_REMOVE_MAPPING);
        if ( ret != 0 )
        {
            PT_LOG("Error: remove old mapping failed!\n");
            return;
        }
    }

    /* map only valid guest address (include 0) */
    if (e_phys != -1)
    {
        /* Create new mapping */
        ret = xc_domain_ioport_mapping(xc_handle, domid, e_phys,
                    assigned_device->bases[i].access.pio_base, e_size,
                    DPCI_ADD_MAPPING);
        if ( ret != 0 )
        {
            PT_LOG("Error: create new mapping failed!\n");
        }
    }
}

/* find emulate register group entry */
static struct pt_reg_grp_tbl* pt_find_reg_grp(struct pt_dev *ptdev,
                                              uint32_t address)
{
    struct pt_reg_grp_tbl* reg_grp_entry = NULL;

    /* find register group entry */
    LIST_FOREACH(reg_grp_entry, &ptdev->reg_grp_tbl_head, entries)
    {
        /* check address */
        if ((reg_grp_entry->base_offset <= address) &&
            ((reg_grp_entry->base_offset + reg_grp_entry->size) > address))
            goto out;
    }
    /* group entry not found */
    reg_grp_entry = NULL;

out:
    return reg_grp_entry;
}

/* find emulate register entry */
static struct pt_reg_tbl* pt_find_reg(struct pt_reg_grp_tbl* reg_grp,
                                      uint32_t address)
{
    struct pt_reg_tbl* reg_entry = NULL;
    struct pt_reg_info_tbl* reg = NULL;
    uint32_t real_offset = 0;

    /* find register entry */
    LIST_FOREACH(reg_entry, &reg_grp->reg_tbl_head, entries)
    {
        reg = reg_entry->reg;
        real_offset = (reg_grp->base_offset + reg->offset);
        /* check address */
        if ((real_offset <= address) && ((real_offset + reg->size) > address))
            goto out;
    }
    /* register entry not found */
    reg_entry = NULL;

out:
    return reg_entry;
}

/* get BAR index */
static int pt_bar_offset_to_index(uint32_t offset)
{
    int index = 0;

    /* check Exp ROM BAR */
    if (offset == PCI_ROM_ADDRESS)
    {
        index = PCI_ROM_SLOT;
        goto out;
    }

    /* calculate BAR index */
    index = ((offset - PCI_BASE_ADDRESS_0) >> 2);
    if (index >= PCI_NUM_REGIONS)
        index = -1;

out:
    return index;
}

static void pt_pci_write_config(PCIDevice *d, uint32_t address, uint32_t val,
                                int len)
{
    struct pt_dev *assigned_device = (struct pt_dev *)d;
    struct pci_dev *pci_dev = assigned_device->pci_dev;
    struct pt_pm_info *pm_state = assigned_device->pm_state;
    struct pt_reg_grp_tbl *reg_grp_entry = NULL;
    struct pt_reg_grp_info_tbl *reg_grp = NULL;
    struct pt_reg_tbl *reg_entry = NULL;
    struct pt_reg_info_tbl *reg = NULL;
    uint32_t find_addr = address;
    uint32_t real_offset = 0;
    uint32_t valid_mask = 0xFFFFFFFF;
    uint32_t read_val = 0;
    uint8_t *ptr_val = NULL;
    int emul_len = 0;
    int index = 0;
    int ret = 0;

#ifdef PT_DEBUG_PCI_CONFIG_ACCESS
    PT_LOG_DEV(d, "address=%04x val=0x%08x len=%d\n", address, val, len);
#endif

    /* check offset range */
    if (address > 0xFF)
    {
        PT_LOG_DEV(d, "Error: Failed to write register with offset exceeding FFh. "
            "[Offset:%02xh][Length:%d]\n", address, len);
        goto exit;
    }

    /* check write size */
    if ((len != 1) && (len != 2) && (len != 4))
    {
        PT_LOG_DEV(d, "Error: Failed to write register with invalid access length. "
            "[Offset:%02xh][Length:%d]\n", address, len);
        goto exit;
    }

    /* check offset alignment */
    if (address & (len-1))
    {
        PT_LOG_DEV(d, "Error: Failed to write register with invalid access size "
            "alignment. [Offset:%02xh][Length:%d]\n",
            address, len);
        goto exit;
    }

    /* check unused BAR register */
    index = pt_bar_offset_to_index(address);
    if ((index >= 0) && (val > 0 && val < PT_BAR_ALLF) &&
        (assigned_device->bases[index].bar_flag == PT_BAR_FLAG_UNUSED))
    {
        PT_LOG_DEV(d, "Warning: Guest attempt to set address to unused Base Address "
            "Register. [Offset:%02xh][Length:%d]\n", address, len);
    }

    /* check power state transition flags */
    if (pm_state != NULL && pm_state->flags & PT_FLAG_TRANSITING)
        /* can't accept untill previous power state transition is completed.
         * so finished previous request here.
         */
        qemu_run_one_timer(pm_state->pm_timer);

    /* find register group entry */
    reg_grp_entry = pt_find_reg_grp(assigned_device, address);
    if (reg_grp_entry)
    {
        reg_grp = reg_grp_entry->reg_grp;
        /* check 0 Hardwired register group */
        if (reg_grp->grp_type == GRP_TYPE_HARDWIRED)
        {
            /* ignore silently */
            PT_LOG_DEV(d, "Warning: Access to 0 Hardwired register. "
                "[Offset:%02xh][Length:%d]\n", address, len);
            goto exit;
        }
    }

    /* read I/O device register value */
    ret = pci_read_block(pci_dev, address, (uint8_t *)&read_val, len);

    if (!ret)
    {
        PT_LOG("Error: pci_read_block failed. return value[%d].\n", ret);
        memset((uint8_t *)&read_val, 0xff, len);
    }

    /* pass directly to libpci for passthrough type register group */
    if (reg_grp_entry == NULL)
        goto out;

    /* adjust the read and write value to appropriate CFC-CFF window */
    read_val <<= ((address & 3) << 3);
    val <<= ((address & 3) << 3);
    emul_len = len;

    /* loop Guest request size */
    while (0 < emul_len)
    {
        /* find register entry to be emulated */
        reg_entry = pt_find_reg(reg_grp_entry, find_addr);
        if (reg_entry)
        {
            reg = reg_entry->reg;
            real_offset = (reg_grp_entry->base_offset + reg->offset);
            valid_mask = (0xFFFFFFFF >> ((4 - emul_len) << 3));
            valid_mask <<= ((find_addr - real_offset) << 3);
            ptr_val = ((uint8_t *)&val + (real_offset & 3));

            /* do emulation depend on register size */
            switch (reg->size) {
            case 1:
                /* emulate write to byte register */
                if (reg->u.b.write)
                    ret = reg->u.b.write(assigned_device, reg_entry,
                               (uint8_t *)ptr_val,
                               (uint8_t)(read_val >> ((real_offset & 3) << 3)),
                               (uint8_t)valid_mask);
                break;
            case 2:
                /* emulate write to word register */
                if (reg->u.w.write)
                    ret = reg->u.w.write(assigned_device, reg_entry,
                               (uint16_t *)ptr_val,
                               (uint16_t)(read_val >> ((real_offset & 3) << 3)),
                               (uint16_t)valid_mask);
                break;
            case 4:
                /* emulate write to double word register */
                if (reg->u.dw.write)
                    ret = reg->u.dw.write(assigned_device, reg_entry,
                               (uint32_t *)ptr_val,
                               (uint32_t)(read_val >> ((real_offset & 3) << 3)),
                               (uint32_t)valid_mask);
                break;
            }

            /* write emulation error */
            if (ret < 0)
            {
                /* exit I/O emulator */
                PT_LOG("Internal error: Invalid write emulation "
                    "return value[%d]. I/O emulator exit.\n", ret);
                exit(1);
            }

            /* calculate next address to find */
            emul_len -= reg->size;
            if (emul_len > 0)
                find_addr = real_offset + reg->size;
        }
        else
        {
            /* nothing to do with passthrough type register,
             * continue to find next byte
             */
            emul_len--;
            find_addr++;
        }
    }

    /* need to shift back before passing them to libpci */
    val >>= ((address & 3) << 3);

out:
    if (!(reg && reg->no_wb)) {  /* unknown regs are passed through */
        ret = pci_write_block(pci_dev, address, (uint8_t *)&val, len);

        if (!ret)
            PT_LOG("Error: pci_write_block failed. return value[%d].\n", ret);
    }

    if (pm_state != NULL && pm_state->flags & PT_FLAG_TRANSITING)
        /* set QEMUTimer */
        qemu_mod_timer(pm_state->pm_timer,
            (qemu_get_clock(rt_clock) + pm_state->pm_delay));

exit:
    return;
}

static uint32_t pt_pci_read_config(PCIDevice *d, uint32_t address, int len)
{
    struct pt_dev *assigned_device = (struct pt_dev *)d;
    struct pci_dev *pci_dev = assigned_device->pci_dev;
    struct pt_pm_info *pm_state = assigned_device->pm_state;
    uint32_t val = 0;
    struct pt_reg_grp_tbl *reg_grp_entry = NULL;
    struct pt_reg_grp_info_tbl *reg_grp = NULL;
    struct pt_reg_tbl *reg_entry = NULL;
    struct pt_reg_info_tbl *reg = NULL;
    uint32_t find_addr = address;
    uint32_t real_offset = 0;
    uint32_t valid_mask = 0xFFFFFFFF;
    uint8_t *ptr_val = NULL;
    int emul_len = 0;
    int ret = 0;

    /* check offset range */
    if (address > 0xFF)
    {
        PT_LOG_DEV(d, "Error: Failed to read register with offset exceeding FFh. "
            "[Offset:%02xh][Length:%d]\n", address, len);
        goto exit;
    }

    /* check read size */
    if ((len != 1) && (len != 2) && (len != 4))
    {
        PT_LOG_DEV(d, "Error: Failed to read register with invalid access length. "
            "[Offset:%02xh][Length:%d]\n", address, len);
        goto exit;
    }

    /* check offset alignment */
    if (address & (len-1))
    {
        PT_LOG_DEV(d, "Error: Failed to read register with invalid access size "
            "alignment. [Offset:%02xh][Length:%d]\n", address, len);
        goto exit;
    }

    /* check power state transition flags */
    if (pm_state != NULL && pm_state->flags & PT_FLAG_TRANSITING)
        /* can't accept untill previous power state transition is completed.
         * so finished previous request here.
         */
        qemu_run_one_timer(pm_state->pm_timer);

    /* find register group entry */
    reg_grp_entry = pt_find_reg_grp(assigned_device, address);
    if (reg_grp_entry)
    {
        reg_grp = reg_grp_entry->reg_grp;
        /* check 0 Hardwired register group */
        if (reg_grp->grp_type == GRP_TYPE_HARDWIRED)
        {
            /* no need to emulate, just return 0 */
            val = 0;
            goto exit;
        }
    }

    /* read I/O device register value */
    ret = pci_read_block(pci_dev, address, (uint8_t *)&val, len);

    if (!ret)
    {
        PT_LOG("Error: pci_read_block failed. return value[%d].\n", ret);
        memset((uint8_t *)&val, 0xff, len);
    }

    /* just return the I/O device register value for
     * passthrough type register group
     */
    if (reg_grp_entry == NULL)
        goto exit;

    /* adjust the read value to appropriate CFC-CFF window */
    val <<= ((address & 3) << 3);
    emul_len = len;

    /* loop Guest request size */
    while (0 < emul_len)
    {
        /* find register entry to be emulated */
        reg_entry = pt_find_reg(reg_grp_entry, find_addr);
        if (reg_entry)
        {
            reg = reg_entry->reg;
            real_offset = (reg_grp_entry->base_offset + reg->offset);
            valid_mask = (0xFFFFFFFF >> ((4 - emul_len) << 3));
            valid_mask <<= ((find_addr - real_offset) << 3);
            ptr_val = ((uint8_t *)&val + (real_offset & 3));

            /* do emulation depend on register size */
            switch (reg->size) {
            case 1:
                /* emulate read to byte register */
                if (reg->u.b.read)
                    ret = reg->u.b.read(assigned_device, reg_entry,
                                        (uint8_t *)ptr_val,
                                        (uint8_t)valid_mask);
                break;
            case 2:
                /* emulate read to word register */
                if (reg->u.w.read)
                    ret = reg->u.w.read(assigned_device, reg_entry,
                                        (uint16_t *)ptr_val,
                                        (uint16_t)valid_mask);
                break;
            case 4:
                /* emulate read to double word register */
                if (reg->u.dw.read)
                    ret = reg->u.dw.read(assigned_device, reg_entry,
                                        (uint32_t *)ptr_val,
                                        (uint32_t)valid_mask);
                break;
            }

            /* read emulation error */
            if (ret < 0)
            {
                /* exit I/O emulator */
                PT_LOG("Internal error: Invalid read emulation "
                    "return value[%d]. I/O emulator exit.\n", ret);
                exit(1);
            }

            /* calculate next address to find */
            emul_len -= reg->size;
            if (emul_len > 0)
                find_addr = real_offset + reg->size;
        }
        else
        {
            /* nothing to do with passthrough type register,
             * continue to find next byte
             */
            emul_len--;
            find_addr++;
        }
    }

    /* need to shift back before returning them to pci bus emulator */
    val >>= ((address & 3) << 3);

exit:

#ifdef PT_DEBUG_PCI_CONFIG_ACCESS
    PT_LOG_DEV(d, "address=%04x val=0x%08x len=%d\n", address, val, len);
#endif

    return val;
}

static void pt_libpci_fixup(struct pci_dev *dev)
{
#if !defined(PCI_LIB_VERSION) || PCI_LIB_VERSION < 0x030100
    int i;
    FILE *fp;
    char path[PATH_MAX], buf[256];
    unsigned long long start, end, flags;

    sprintf(path, "/sys/bus/pci/devices/%04x:%02x:%02x.%x/resource",
            dev->domain, dev->bus, dev->dev, dev->func);
    fp = fopen(path, "r");
    if ( !fp )
    {
        PT_LOG("Error: Can't open %s: %s\n", path, strerror(errno));
        return;
    }

    for ( i = 0; i < PCI_NUM_REGIONS; i++ )
    {
        if ( fscanf(fp, "%llx %llx %llx", &start, &end, &flags) != 3 )
        {
            PT_LOG("Error: Syntax error in %s\n", path);
            break;
        }

        flags &= 0xf;

        if ( i < PCI_ROM_SLOT )
            dev->base_addr[i] |= flags;
        else
            dev->rom_base_addr |= flags;
    }

    fclose(fp);
#endif /* PCI_LIB_VERSION < 0x030100 */
}

static int pt_dev_is_virtfn(struct pci_dev *dev)
{
    int rc;
    char path[PATH_MAX];
    struct stat buf;

    sprintf(path, "/sys/bus/pci/devices/%04x:%02x:%02x.%x/physfn",
            dev->domain, dev->bus, dev->dev, dev->func);

    rc = !stat(path, &buf);
    if ( rc )
        PT_LOG("%04x:%02x:%02x.%x is a SR-IOV Virtual Function\n",
               dev->domain, dev->bus, dev->dev, dev->func);

    return rc;
}

static int pt_register_regions(struct pt_dev *assigned_device)
{
    int i = 0;
    uint32_t bar_data = 0;
    struct pci_dev *pci_dev = assigned_device->pci_dev;
    PCIDevice *d = &assigned_device->dev;
    int ret;

    /* Register PIO/MMIO BARs */
    for ( i = 0; i < PCI_BAR_ENTRIES; i++ )
    {
        if ( pt_pci_base_addr(pci_dev->base_addr[i]) )
        {
            assigned_device->bases[i].e_physbase =
                    pt_pci_base_addr(pci_dev->base_addr[i]);
            assigned_device->bases[i].access.u =
                    pt_pci_base_addr(pci_dev->base_addr[i]);

            /* Register current region */
            if ( pci_dev->base_addr[i] & PCI_ADDRESS_SPACE_IO )
                pci_register_io_region((PCIDevice *)assigned_device, i,
                    (uint32_t)pci_dev->size[i], PCI_ADDRESS_SPACE_IO,
                    pt_ioport_map);
            else if ( pci_dev->base_addr[i] & PCI_ADDRESS_SPACE_MEM_PREFETCH )
                pci_register_io_region((PCIDevice *)assigned_device, i,
                    (uint32_t)pci_dev->size[i], PCI_ADDRESS_SPACE_MEM_PREFETCH,
                    pt_iomem_map);
            else
                pci_register_io_region((PCIDevice *)assigned_device, i,
                    (uint32_t)pci_dev->size[i], PCI_ADDRESS_SPACE_MEM,
                    pt_iomem_map);

            PT_LOG("IO region registered (size=0x%08x base_addr=0x%08x)\n",
                (uint32_t)(pci_dev->size[i]),
                (uint32_t)(pci_dev->base_addr[i]));
        }
    }

    /* Register expansion ROM address */
    if ( (pci_dev->rom_base_addr & PCI_ROM_ADDRESS_MASK) && pci_dev->rom_size )
    {

        /* Re-set BAR reported by OS, otherwise ROM can't be read. */
        bar_data = pci_read_long(pci_dev, PCI_ROM_ADDRESS);
        if ( (bar_data & PCI_ROM_ADDRESS_MASK) == 0 )
        {
            bar_data |= (pci_dev->rom_base_addr & PCI_ROM_ADDRESS_MASK);
            pci_write_long(pci_dev, PCI_ROM_ADDRESS, bar_data);
        }

        assigned_device->bases[PCI_ROM_SLOT].e_physbase =
            pci_dev->rom_base_addr & PCI_ROM_ADDRESS_MASK;
        assigned_device->bases[PCI_ROM_SLOT].access.maddr =
            pci_dev->rom_base_addr & PCI_ROM_ADDRESS_MASK;
        pci_register_io_region((PCIDevice *)assigned_device, PCI_ROM_SLOT,
            pci_dev->rom_size, PCI_ADDRESS_SPACE_MEM_PREFETCH,
            pt_iomem_map);

        PT_LOG("Expansion ROM registered (size=0x%08x base_addr=0x%08x)\n",
            (uint32_t)(pci_dev->rom_size), (uint32_t)(pci_dev->rom_base_addr));
    }
    register_vga_regions(assigned_device);
    return 0;
}

static void pt_unregister_regions(struct pt_dev *assigned_device)
{
    int i, type, ret;
    uint32_t e_size;
    PCIDevice *d = (PCIDevice*)assigned_device;

    for ( i = 0; i < PCI_NUM_REGIONS; i++ )
    {
        e_size = assigned_device->bases[i].e_size;
        if ( (e_size == 0) || (assigned_device->bases[i].e_physbase == -1) )
            continue;

        type = d->io_regions[i].type;

        if ( type == PCI_ADDRESS_SPACE_MEM ||
             type == PCI_ADDRESS_SPACE_MEM_PREFETCH )
        {
            ret = _pt_iomem_helper(assigned_device, i,
                                   assigned_device->bases[i].e_physbase,
                                   e_size, DPCI_REMOVE_MAPPING);
            if ( ret != 0 )
            {
                PT_LOG("Error: remove old mem mapping failed!\n");
                continue;
            }

        }
        else if ( type == PCI_ADDRESS_SPACE_IO )
        {
            ret = xc_domain_ioport_mapping(xc_handle, domid,
                        assigned_device->bases[i].e_physbase,
                        assigned_device->bases[i].access.pio_base,
                        e_size,
                        DPCI_REMOVE_MAPPING);
            if ( ret != 0 )
            {
                PT_LOG("Error: remove old io mapping failed!\n");
                continue;
            }

        }

    }
    unregister_vga_regions(assigned_device);
}

static uint8_t find_cap_offset(struct pci_dev *pci_dev, uint8_t cap)
{
    int id;
    int max_cap = 48;
    int pos = PCI_CAPABILITY_LIST;
    int status;

    status = pci_read_byte(pci_dev, PCI_STATUS);
    if ( (status & PCI_STATUS_CAP_LIST) == 0 )
        return 0;

    while ( max_cap-- )
    {
        pos = pci_read_byte(pci_dev, pos);
        if ( pos < 0x40 )
            break;

        pos &= ~3;
        id = pci_read_byte(pci_dev, pos + PCI_CAP_LIST_ID);

        if ( id == 0xff )
            break;
        if ( id == cap )
            return pos;

        pos += PCI_CAP_LIST_NEXT;
    }
    return 0;
}

static uint32_t find_ext_cap_offset(struct pci_dev *pci_dev, uint32_t cap)
{
    uint32_t header = 0;
    int max_cap = 480;
    int pos = 0x100;

    do
    {
        header = pci_read_long(pci_dev, pos);
        /*
         * If we have no capabilities, this is indicated by cap ID,
         * cap version and next pointer all being 0.
         */
        if (header == 0)
            break;

        if (PCI_EXT_CAP_ID(header) == cap)
            return pos;

        pos = PCI_EXT_CAP_NEXT(header);
        if (pos < 0x100)
            break;

        max_cap--;
    }while (max_cap > 0);

    return 0;
}

static void pci_access_init(void)
{
    struct pci_access *pci_access;

    if (dpci_infos.pci_access)
        return;

    /* Initialize libpci */
    pci_access = pci_alloc();
    if ( pci_access == NULL ) {
        PT_LOG("Error: pci_access is NULL\n");
        return;
    }
    pci_init(pci_access);
    pci_scan_bus(pci_access);
    dpci_infos.pci_access = pci_access;
}

struct pci_dev *pt_pci_get_dev(int bus, int dev, int fn)
{
    pci_access_init();
    return pci_get_dev(dpci_infos.pci_access, 0, bus, dev, fn);
}

u32 pt_pci_host_read(struct pci_dev *pci_dev, u32 addr, int len)
{
    u32 val = -1;

    pci_access_init();
    pci_read_block(pci_dev, addr, (u8 *) &val, len);
    return val;
}

int pt_pci_host_write(struct pci_dev *pci_dev, u32 addr, u32 val, int len)
{
    int ret = 0;

    pci_access_init();
    ret = pci_write_block(pci_dev, addr, (u8 *) &val, len);
    return ret;
}

/* parse BAR */
static int pt_bar_reg_parse(
        struct pt_dev *ptdev, struct pt_reg_info_tbl *reg)
{
    PCIDevice *d = &ptdev->dev;
    struct pt_region *region = NULL;
    PCIIORegion *r;
    int bar_flag = PT_BAR_FLAG_UNUSED;
    int index = 0;
    int i;

    /* check 64bit BAR */
    index = pt_bar_offset_to_index(reg->offset);
    if ((index > 0) && (index < PCI_ROM_SLOT) &&
        ((ptdev->pci_dev->base_addr[index-1] & (PCI_BASE_ADDRESS_SPACE |
                               PCI_BASE_ADDRESS_MEM_TYPE_MASK)) ==
         (PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64)))
    {
        region = &ptdev->bases[index-1];
        if (region->bar_flag != PT_BAR_FLAG_UPPER)
        {
            bar_flag = PT_BAR_FLAG_UPPER;
            goto out;
        }
    }

    /* check unused BAR */
    r = &d->io_regions[index];
    if (!r->size)
        goto out;

    /* for ExpROM BAR */
    if (index == PCI_ROM_SLOT)
    {
        bar_flag = PT_BAR_FLAG_MEM;
        goto out;
    }

    /* check BAR I/O indicator */
    if ( ptdev->pci_dev->base_addr[index] & PCI_BASE_ADDRESS_SPACE_IO )
        bar_flag = PT_BAR_FLAG_IO;
    else
        bar_flag = PT_BAR_FLAG_MEM;

out:
    return bar_flag;
}

/* mapping BAR */
static void pt_bar_mapping_one(struct pt_dev *ptdev, int bar, int io_enable,
    int mem_enable)
{
    PCIDevice *dev = (PCIDevice *)&ptdev->dev;
    PCIIORegion *r;
    struct pt_reg_grp_tbl *reg_grp_entry = NULL;
    struct pt_reg_tbl *reg_entry = NULL;
    struct pt_region *base = NULL;
    uint32_t r_size = 0, r_addr = -1;
    int ret = 0;

    r = &dev->io_regions[bar];

    /* check valid region */
    if (!r->size)
        return;

    base = &ptdev->bases[bar];
    /* skip unused BAR or upper 64bit BAR */
    if ((base->bar_flag == PT_BAR_FLAG_UNUSED) ||
       (base->bar_flag == PT_BAR_FLAG_UPPER))
           return;

    /* copy region address to temporary */
    r_addr = r->addr;

    /* need unmapping in case I/O Space or Memory Space disable */
    if (((base->bar_flag == PT_BAR_FLAG_IO) && !io_enable ) ||
        ((base->bar_flag == PT_BAR_FLAG_MEM) && !mem_enable ))
        r_addr = -1;
    if ( (bar == PCI_ROM_SLOT) && (r_addr != -1) )
    {
        reg_grp_entry = pt_find_reg_grp(ptdev, PCI_ROM_ADDRESS);
        if (reg_grp_entry)
        {
            reg_entry = pt_find_reg(reg_grp_entry, PCI_ROM_ADDRESS);
            if (reg_entry && !(reg_entry->data & PCI_ROM_ADDRESS_ENABLE))
                r_addr = -1;
        }
    }

    /* prevent guest software mapping memory resource to 00000000h */
    if ((base->bar_flag == PT_BAR_FLAG_MEM) && (r_addr == 0))
        r_addr = -1;

    /* align resource size (memory type only) */
    r_size = r->size;
    PT_GET_EMUL_SIZE(base->bar_flag, r_size);

    /* check overlapped address */
    ret = pt_chk_bar_overlap(dev->bus, dev->devfn,
                    r_addr, r_size, r->type);
    if (ret > 0)
        PT_LOG_DEV(dev, "Warning: [Region:%d][Address:%08xh]"
            "[Size:%08xh] is overlapped.\n", bar, r_addr, r_size);

    /* check whether we need to update the mapping or not */
    if (r_addr != ptdev->bases[bar].e_physbase)
    {
        /* mapping BAR */
        r->map_func((PCIDevice *)ptdev, bar, r_addr,
                     r_size, r->type);
    }
}

static void pt_bar_mapping(struct pt_dev *ptdev, int io_enable, int mem_enable)
{
    int i;

    for (i=0; i<PCI_NUM_REGIONS; i++)
        pt_bar_mapping_one(ptdev, i, io_enable, mem_enable);
}

/* check power state transition */
static int check_power_state(struct pt_dev *ptdev)
{
    struct pt_pm_info *pm_state = ptdev->pm_state;
    PCIDevice *d = &ptdev->dev;
    uint16_t read_val = 0;
    uint16_t cur_state = 0;

    /* get current power state */
    read_val = pci_read_word(ptdev->pci_dev,
                                (pm_state->pm_base + PCI_PM_CTRL));
    cur_state = read_val & PCI_PM_CTRL_STATE_MASK;

    if (pm_state->req_state != cur_state)
    {
        PT_LOG_DEV(d, "Error: Failed to change power state. "
            "[requested state:%d][current state:%d]\n",
            pm_state->req_state, cur_state);
        return -1;
    }
    return 0;
}

/* save AER one register */
static void aer_save_one_register(struct pt_dev *ptdev, int offset)
{
    PCIDevice *d = &ptdev->dev;
    uint32_t aer_base = ptdev->pm_state->aer_base;

    *(uint32_t*)(d->config + (aer_base + offset))
        = pci_read_long(ptdev->pci_dev, (aer_base + offset));
}

/* save AER registers */
static void pt_aer_reg_save(struct pt_dev *ptdev)
{
    /* after reset, following register values should be restored.
     * So, save them.
     */
    aer_save_one_register(ptdev, PCI_ERR_UNCOR_MASK);
    aer_save_one_register(ptdev, PCI_ERR_UNCOR_SEVER);
    aer_save_one_register(ptdev, PCI_ERR_COR_MASK);
    aer_save_one_register(ptdev, PCI_ERR_CAP);
}

/* restore AER one register */
static void aer_restore_one_register(struct pt_dev *ptdev, int offset)
{
    PCIDevice *d = &ptdev->dev;
    uint32_t aer_base = ptdev->pm_state->aer_base;
    uint32_t config = 0;

    config = *(uint32_t*)(d->config + (aer_base + offset));
    pci_write_long(ptdev->pci_dev, (aer_base + offset), config);
}

/* restore AER registers */
static void pt_aer_reg_restore(struct pt_dev *ptdev)
{
    /* the following registers should be reconfigured to correct values
     * after reset. restore them.
     * other registers should not be reconfigured after reset
     * if there is no reason
     */
    aer_restore_one_register(ptdev, PCI_ERR_UNCOR_MASK);
    aer_restore_one_register(ptdev, PCI_ERR_UNCOR_SEVER);
    aer_restore_one_register(ptdev, PCI_ERR_COR_MASK);
    aer_restore_one_register(ptdev, PCI_ERR_CAP);
}

/* reset Interrupt and I/O resource  */
static void pt_reset_interrupt_and_io_mapping(struct pt_dev *ptdev)
{
    PCIDevice *d = &ptdev->dev;
    PCIIORegion *r;
    int i = 0;
    uint8_t e_device = 0;
    uint8_t e_intx = 0;

    /* unbind INTx */
    e_device = PCI_SLOT(ptdev->dev.devfn);
    e_intx = pci_intx(ptdev);

    if (ptdev->msi_trans_en == 0 && ptdev->machine_irq)
    {
        if (xc_domain_unbind_pt_irq(xc_handle, domid, ptdev->machine_irq,
                        PT_IRQ_TYPE_PCI, 0, e_device, e_intx, 0))
            PT_LOG("Error: Unbinding of interrupt failed!\n");
    }

    /* disable MSI/MSI-X and MSI-INTx translation */
    if (ptdev->msi)
        pt_msi_disable(ptdev);
    if (ptdev->msix)
        pt_msix_disable(ptdev);

    /* clear all virtual region address */
    for (i=0; i<PCI_NUM_REGIONS; i++)
    {
        r = &d->io_regions[i];
        r->addr = -1;
    }

    /* unmapping BAR */
    pt_bar_mapping(ptdev, 0, 0);
}

/* restore a part of I/O device register */
static void pt_config_restore(struct pt_dev *ptdev)
{
    struct pt_reg_grp_tbl *reg_grp_entry = NULL;
    struct pt_reg_grp_info_tbl *reg_grp = NULL;
    struct pt_reg_tbl *reg_entry = NULL;
    struct pt_reg_info_tbl *reg = NULL;
    uint32_t real_offset = 0;
    uint32_t read_val = 0;
    uint32_t val = 0;
    int ret = 0;
    PCIDevice *d = &ptdev->dev;

    /* find emulate register group entry */
    LIST_FOREACH(reg_grp_entry, &ptdev->reg_grp_tbl_head, entries)
    {
        /* find emulate register entry */
        LIST_FOREACH(reg_entry, &reg_grp_entry->reg_tbl_head, entries)
        {
            reg = reg_entry->reg;

            /* check whether restoring is needed */
            if (!reg->u.b.restore)
                continue;

            real_offset = (reg_grp_entry->base_offset + reg->offset);

            /* read I/O device register value */
            ret = pci_read_block(ptdev->pci_dev, real_offset,
                        (uint8_t *)&read_val, reg->size);

            if (!ret)
            {
                PT_LOG("Error: pci_read_block failed. "
                    "return value[%d].\n", ret);
                memset((uint8_t *)&read_val, 0xff, reg->size);
            }

            val = 0;

            /* restore based on register size */
            switch (reg->size) {
            case 1:
                /* byte register */
                ret = reg->u.b.restore(ptdev, reg_entry, real_offset,
                           (uint8_t)read_val, (uint8_t *)&val);
                break;
            case 2:
                /* word register */
                ret = reg->u.w.restore(ptdev, reg_entry, real_offset,
                           (uint16_t)read_val, (uint16_t *)&val);
                break;
            case 4:
                /* double word register */
                ret = reg->u.dw.restore(ptdev, reg_entry, real_offset,
                           (uint32_t)read_val, (uint32_t *)&val);
                break;
            }

            /* restoring error */
            if (ret < 0)
            {
                /* exit I/O emulator */
                PT_LOG("Internal error: Invalid restoring "
                    "return value[%d]. I/O emulator exit.\n", ret);
                exit(1);
            }

#ifdef PT_DEBUG_PCI_CONFIG_ACCESS
            PT_LOG_DEV(d, "address=%04x val=0x%08x len=%d\n",
                    real_offset, val, reg->size);
#endif

            ret = pci_write_block(ptdev->pci_dev, real_offset,
                            (uint8_t *)&val, reg->size);

            if (!ret)
                PT_LOG("Error: pci_write_block failed. "
                    "return value[%d].\n", ret);
        }
    }

    /* if AER supported, restore it */
    if (ptdev->pm_state->aer_base)
        pt_aer_reg_restore(ptdev);
}

/* reinitialize all emulate registers */
static void pt_config_reinit(struct pt_dev *ptdev)
{
    struct pt_reg_grp_tbl *reg_grp_entry = NULL;
    struct pt_reg_grp_info_tbl *reg_grp = NULL;
    struct pt_reg_tbl *reg_entry = NULL;
    struct pt_reg_info_tbl *reg = NULL;

    /* find emulate register group entry */
    LIST_FOREACH(reg_grp_entry, &ptdev->reg_grp_tbl_head, entries)
    {
        /* find emulate register entry */
        LIST_FOREACH(reg_entry, &reg_grp_entry->reg_tbl_head, entries)
        {
            reg = reg_entry->reg;
            if (reg->init)
                /* initialize emulate register */
                reg_entry->data = reg->init(ptdev, reg_entry->reg,
                                   (reg_grp_entry->base_offset + reg->offset));
        }
    }
}

static int pt_init_pci_config(struct pt_dev *ptdev)
{
    PCIDevice *d = &ptdev->dev;
    int ret = 0;

    PT_LOG_DEV(d, "Reinitialize PCI configuration registers "
        "due to power state transition with internal reset.\n");

    /* restore a part of I/O device register */
    pt_config_restore(ptdev);

    /* reinitialize all emulate register */
    pt_config_reinit(ptdev);

    /* setup MSI-INTx translation if support */
    ret = pt_enable_msi_translate(ptdev);

    /* rebind machine_irq to device */
    if (ret < 0 && ptdev->machine_irq != 0)
    {
        uint8_t e_device = PCI_SLOT(ptdev->dev.devfn);
        uint8_t e_intx = pci_intx(ptdev);

        ret = xc_domain_bind_pt_pci_irq(xc_handle, domid, ptdev->machine_irq,
                                       0, e_device, e_intx);
        if (ret < 0)
            PT_LOG("Error: Rebinding of interrupt failed! ret=%d\n", ret);
    }

    return ret;
}

static void pt_from_d3hot_to_d0_with_reset(void *opaque)
{
    struct pt_dev *ptdev = opaque;
    PCIDevice *d = &ptdev->dev;
    struct pt_pm_info *pm_state = ptdev->pm_state;
    int ret = 0;

    /* check power state */
    ret = check_power_state(ptdev);

    if (ret < 0)
        goto out;

    pt_init_pci_config(ptdev);

out:
    /* power state transition flags off */
    pm_state->flags &= ~PT_FLAG_TRANSITING;

    qemu_free_timer(pm_state->pm_timer);
    pm_state->pm_timer = NULL;
}

static void pt_default_power_transition(void *opaque)
{
    struct pt_dev *ptdev = opaque;
    struct pt_pm_info *pm_state = ptdev->pm_state;

    /* check power state */
    check_power_state(ptdev);

    /* power state transition flags off */
    pm_state->flags &= ~PT_FLAG_TRANSITING;

    qemu_free_timer(pm_state->pm_timer);
    pm_state->pm_timer = NULL;
}

/* initialize emulate register */
static int pt_config_reg_init(struct pt_dev *ptdev,
        struct pt_reg_grp_tbl *reg_grp,
        struct pt_reg_info_tbl *reg)
{
    struct pt_reg_tbl *reg_entry;
    uint32_t data = 0;
    int err = 0;

    /* allocate register entry */
    reg_entry = qemu_mallocz(sizeof(struct pt_reg_tbl));
    if (reg_entry == NULL)
    {
        PT_LOG("Error: Failed to allocate memory.\n");
        err = -1;
        goto out;
    }

    /* initialize register entry */
    reg_entry->reg = reg;
    reg_entry->data = 0;

    if (reg->init)
    {
        /* initialize emulate register */
        data = reg->init(ptdev, reg_entry->reg,
                        (reg_grp->base_offset + reg->offset));
        if (data == PT_INVALID_REG)
        {
            /* free unused BAR register entry */
            free(reg_entry);
            goto out;
        }
        /* set register value */
        reg_entry->data = data;
    }
    /* list add register entry */
    LIST_INSERT_HEAD(&reg_grp->reg_tbl_head, reg_entry, entries);

out:
    return err;
}

/* A return value of 1 means the capability should NOT be exposed to guest. */
static int pt_hide_dev_cap(const struct pci_dev *dev, uint8_t grp_id)
{
    switch (grp_id)
    {
    case PCI_CAP_ID_EXP:
        /* The PCI Express Capability Structure of the VF of Intel 82599 10GbE
         * Controller looks trivial, e.g., the PCI Express Capabilities
         * Register is 0. We should not try to expose it to guest.
         */
        if (dev->vendor_id == PCI_VENDOR_ID_INTEL &&
                dev->device_id == PCI_DEVICE_ID_INTEL_82599_VF)
            return 1;
        break;
    }

    return 0;
}

/* initialize emulate register group */
static int pt_config_init(struct pt_dev *ptdev)
{
    struct pt_reg_grp_tbl *reg_grp_entry = NULL;
    struct pt_reg_info_tbl *reg_tbl = NULL;
    uint32_t reg_grp_offset = 0;
    int i, j, err = 0;

    /* initialize register group list */
    LIST_INIT(&ptdev->reg_grp_tbl_head);

    /* initialize register group */
    for (i=0; pt_emu_reg_grp_tbl[i].grp_size != 0; i++)
    {
        if (pt_emu_reg_grp_tbl[i].grp_id != 0xFF)
        {
            if (pt_hide_dev_cap(ptdev->pci_dev, pt_emu_reg_grp_tbl[i].grp_id))
                continue;

            reg_grp_offset = (uint32_t)find_cap_offset(ptdev->pci_dev,
                                 pt_emu_reg_grp_tbl[i].grp_id);
            if (!reg_grp_offset)
                continue;
        }

        /* allocate register group table */
        reg_grp_entry = qemu_mallocz(sizeof(struct pt_reg_grp_tbl));
        if (reg_grp_entry == NULL)
        {
            PT_LOG("Error: Failed to allocate memory.\n");
            err = -1;
            goto out;
        }

        /* initialize register group entry */
        LIST_INIT(&reg_grp_entry->reg_tbl_head);

        /* need to declare here, to enable searching Cap Ptr reg
         * (which is in the same reg group) when initializing Status reg
         */
        LIST_INSERT_HEAD(&ptdev->reg_grp_tbl_head, reg_grp_entry, entries);

        reg_grp_entry->base_offset = reg_grp_offset;
        reg_grp_entry->reg_grp =
                (struct pt_reg_grp_info_tbl*)&pt_emu_reg_grp_tbl[i];
        if (pt_emu_reg_grp_tbl[i].size_init)
        {
            /* get register group size */
            reg_grp_entry->size = pt_emu_reg_grp_tbl[i].size_init(ptdev,
                                      reg_grp_entry->reg_grp,
                                      reg_grp_offset);
        }

        if (pt_emu_reg_grp_tbl[i].grp_type == GRP_TYPE_EMU)
        {
            if (pt_emu_reg_grp_tbl[i].emu_reg_tbl)
            {
                reg_tbl = pt_emu_reg_grp_tbl[i].emu_reg_tbl;
                /* initialize capability register */
                for (j=0; reg_tbl->size != 0; j++, reg_tbl++)
                {
                    /* initialize capability register */
                    err = pt_config_reg_init(ptdev, reg_grp_entry, reg_tbl);
                    if (err < 0)
                        goto out;
                }
            }
        }
        reg_grp_offset = 0;
    }

out:
    return err;
}

/* delete all emulate register */
static void pt_config_delete(struct pt_dev *ptdev)
{
    struct pt_reg_grp_tbl *reg_grp_entry = NULL;
    struct pt_reg_tbl *reg_entry = NULL;

    /* free MSI/MSI-X info table */
    if (ptdev->msix)
        pt_msix_delete(ptdev);
    if (ptdev->msi)
        free(ptdev->msi);

    /* free Power Management info table */
    if (ptdev->pm_state)
    {
        if (ptdev->pm_state->pm_timer)
        {
            qemu_del_timer(ptdev->pm_state->pm_timer);
            qemu_free_timer(ptdev->pm_state->pm_timer);
            ptdev->pm_state->pm_timer = NULL;
        }

        free(ptdev->pm_state);
    }

    /* free all register group entry */
    while((reg_grp_entry = LIST_FIRST(&ptdev->reg_grp_tbl_head)) != NULL)
    {
        /* free all register entry */
        while((reg_entry = LIST_FIRST(&reg_grp_entry->reg_tbl_head)) != NULL)
        {
            LIST_REMOVE(reg_entry, entries);
            qemu_free(reg_entry);
        }

        LIST_REMOVE(reg_grp_entry, entries);
        qemu_free(reg_grp_entry);
    }
}

/* initialize common register value */
static uint32_t pt_common_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    return reg->init_val;
}

/* initialize Vendor ID register value */
static uint32_t pt_vendor_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    return ptdev->pci_dev->vendor_id;
}

/* initialize Device ID register value */
static uint32_t pt_device_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    return ptdev->pci_dev->device_id;
}

/* initialize Capabilities Pointer or Next Pointer register */
static uint32_t pt_ptr_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    uint32_t reg_field = (uint32_t)ptdev->dev.config[real_offset];
    int i;

    /* find capability offset */
    while (reg_field)
    {
        for (i=0; pt_emu_reg_grp_tbl[i].grp_size != 0; i++)
        {
            /* check whether the next capability
             * should be exported to guest or not
             */
            if (pt_hide_dev_cap(ptdev->pci_dev, pt_emu_reg_grp_tbl[i].grp_id))
                continue;
            if (pt_emu_reg_grp_tbl[i].grp_id == ptdev->dev.config[reg_field])
            {
                if (pt_emu_reg_grp_tbl[i].grp_type == GRP_TYPE_EMU)
                    goto out;
                /* ignore the 0 hardwired capability, find next one */
                break;
            }
        }
        /* next capability */
        reg_field = (uint32_t)ptdev->dev.config[reg_field + 1];
    }

out:
    return reg_field;
}

/* initialize Status register */
static uint32_t pt_status_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    struct pt_reg_grp_tbl *reg_grp_entry = NULL;
    struct pt_reg_tbl *reg_entry = NULL;
    int reg_field = 0;

    /* find Header register group */
    reg_grp_entry = pt_find_reg_grp(ptdev, PCI_CAPABILITY_LIST);
    if (reg_grp_entry)
    {
        /* find Capabilities Pointer register */
        reg_entry = pt_find_reg(reg_grp_entry, PCI_CAPABILITY_LIST);
        if (reg_entry)
        {
            /* check Capabilities Pointer register */
            if (reg_entry->data)
                reg_field |= PCI_STATUS_CAP_LIST;
            else
                reg_field &= ~PCI_STATUS_CAP_LIST;
        }
        else
        {
            /* exit I/O emulator */
            PT_LOG("Internal error: Couldn't find pt_reg_tbl for "
                "Capabilities Pointer register. I/O emulator exit.\n");
            exit(1);
        }
    }
    else
    {
        /* exit I/O emulator */
        PT_LOG("Internal error: Couldn't find pt_reg_grp_tbl for Header. "
            "I/O emulator exit.\n");
        exit(1);
    }

    return reg_field;
}

/* initialize Interrupt Pin register */
static uint32_t pt_irqpin_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    return pci_read_intx(ptdev);
}

/* initialize BAR */
static uint32_t pt_bar_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    int reg_field = 0;
    int index;

    /* get BAR index */
    index = pt_bar_offset_to_index(reg->offset);
    if (index < 0)
    {
        /* exit I/O emulator */
        PT_LOG("Internal error: Invalid BAR index[%d]. "
            "I/O emulator exit.\n", index);
        exit(1);
    }

    /* set initial guest physical base address to -1 */
    ptdev->bases[index].e_physbase = -1;

    /* set BAR flag */
    ptdev->bases[index].bar_flag = pt_bar_reg_parse(ptdev, reg);
    if (ptdev->bases[index].bar_flag == PT_BAR_FLAG_UNUSED)
        reg_field = PT_INVALID_REG;

    return reg_field;
}

/* initialize Power Management Capabilities register */
static uint32_t pt_pmc_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    PCIDevice *d = &ptdev->dev;

    if (!ptdev->power_mgmt)
        return reg->init_val;

    /* set Power Management Capabilities register */
    ptdev->pm_state->pmc_field = *(uint16_t *)(d->config + real_offset);

    return reg->init_val;
}

/* initialize PCI Power Management Control/Status register */
static uint32_t pt_pmcsr_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    PCIDevice *d = &ptdev->dev;
    uint16_t cap_ver  = 0;

    if (!ptdev->power_mgmt)
        return reg->init_val;

    /* check PCI Power Management support version */
    cap_ver = ptdev->pm_state->pmc_field & PCI_PM_CAP_VER_MASK;

    if (cap_ver > 2)
        /* set No Soft Reset */
        ptdev->pm_state->no_soft_reset = (*(uint8_t *)(d->config + real_offset)
            & (uint8_t)PCI_PM_CTRL_NO_SOFT_RESET);

    /* wake up real physical device */
    switch ( pci_read_word(ptdev->pci_dev, real_offset) 
             & PCI_PM_CTRL_STATE_MASK )
    {
    case 0:
        break;
    case 1:
        PT_LOG("Power state transition D1 -> D0active\n");
        pci_write_word(ptdev->pci_dev, real_offset, 0);
        break;
    case 2:
        PT_LOG("Power state transition D2 -> D0active\n");
        pci_write_word(ptdev->pci_dev, real_offset, 0);
        usleep(200);
        break;
    case 3:
        PT_LOG("Power state transition D3hot -> D0active\n");
        pci_write_word(ptdev->pci_dev, real_offset, 0);
        usleep(10 * 1000);
        pt_init_pci_config(ptdev);
        break;
    }

    return reg->init_val;
}

/* initialize Link Control register */
static uint32_t pt_linkctrl_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    uint8_t cap_ver = 0;
    uint8_t dev_type = 0;

    cap_ver = (ptdev->dev.config[(real_offset - reg->offset) + PCI_EXP_FLAGS] &
        (uint8_t)PCI_EXP_FLAGS_VERS);
    dev_type = (ptdev->dev.config[(real_offset - reg->offset) + PCI_EXP_FLAGS] &
        (uint8_t)PCI_EXP_FLAGS_TYPE) >> 4;

    /* no need to initialize in case of Root Complex Integrated Endpoint
     * with cap_ver 1.x
     */
    if ((dev_type == PCI_EXP_TYPE_ROOT_INT_EP) && (cap_ver == 1))
        return PT_INVALID_REG;

    return reg->init_val;
}

/* initialize Device Control 2 register */
static uint32_t pt_devctrl2_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    uint8_t cap_ver = 0;

    cap_ver = (ptdev->dev.config[(real_offset - reg->offset) + PCI_EXP_FLAGS] &
        (uint8_t)PCI_EXP_FLAGS_VERS);

    /* no need to initialize in case of cap_ver 1.x */
    if (cap_ver == 1)
        return PT_INVALID_REG;

    return reg->init_val;
}

/* initialize Link Control 2 register */
static uint32_t pt_linkctrl2_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    int reg_field = 0;
    uint8_t cap_ver = 0;

    cap_ver = (ptdev->dev.config[(real_offset - reg->offset) + PCI_EXP_FLAGS] &
        (uint8_t)PCI_EXP_FLAGS_VERS);

    /* no need to initialize in case of cap_ver 1.x */
    if (cap_ver == 1)
        return PT_INVALID_REG;

    /* set Supported Link Speed */
    reg_field |=
        (0x0F &
         ptdev->dev.config[(real_offset - reg->offset) + PCI_EXP_LNKCAP]);

    return reg_field;
}

/* initialize Message Control register */
static uint32_t pt_msgctrl_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    PCIDevice *d = (struct PCIDevice *)ptdev;
    struct pci_dev *pdev = ptdev->pci_dev;
    uint32_t reg_field = 0;

    /* use I/O device register's value as initial value */
    reg_field = *((uint16_t*)(d->config + real_offset));

    if (reg_field & PCI_MSI_FLAGS_ENABLE)
    {
        PT_LOG("MSI enabled already, disable first\n");
        pci_write_word(pdev, real_offset, reg_field & ~PCI_MSI_FLAGS_ENABLE);
    }
    ptdev->msi->flags |= (reg_field | MSI_FLAG_UNINIT);
    ptdev->msi->ctrl_offset = real_offset;

    return reg->init_val;
}

/* initialize Message Upper Address register */
static uint32_t pt_msgaddr64_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    /* no need to initialize in case of 32 bit type */
    if (!(ptdev->msi->flags & PCI_MSI_FLAGS_64BIT))
        return PT_INVALID_REG;

    return reg->init_val;
}

/* this function will be called twice (for 32 bit and 64 bit type) */
/* initialize Message Data register */
static uint32_t pt_msgdata_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    uint32_t flags = ptdev->msi->flags;
    uint32_t offset = reg->offset;

    /* check the offset whether matches the type or not */
    if (((offset == PCI_MSI_DATA_64) &&  (flags & PCI_MSI_FLAGS_64BIT)) ||
        ((offset == PCI_MSI_DATA_32) && !(flags & PCI_MSI_FLAGS_64BIT)))
        return reg->init_val;
    else
        return PT_INVALID_REG;
}

/* initialize Message Control register for MSI-X */
static uint32_t pt_msixctrl_reg_init(struct pt_dev *ptdev,
        struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    PCIDevice *d = (struct PCIDevice *)ptdev;
    struct pci_dev *pdev = ptdev->pci_dev;
    uint16_t reg_field = 0;

    /* use I/O device register's value as initial value */
    reg_field = *((uint16_t*)(d->config + real_offset));

    if (reg_field & PCI_MSIX_ENABLE)
    {
        PT_LOG("MSIX enabled already, disable first\n");
        pci_write_word(pdev, real_offset, reg_field & ~PCI_MSIX_ENABLE);
    }

    ptdev->msix->ctrl_offset = real_offset;

    return reg->init_val;
}

static uint8_t pt_reg_grp_header0_size_init(struct pt_dev *ptdev,
        struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset)
{
    /*
    ** By default we will trap up to 0x40 in the cfg space.
    ** If an intel device is pass through we need to trap 0xfc,
    ** therefore the size should be 0xff.
    */
    if (igd_passthru)
        return 0xFF;
    return grp_reg->grp_size;
}

/* get register group size */
static uint8_t pt_reg_grp_size_init(struct pt_dev *ptdev,
        struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset)
{
    return grp_reg->grp_size;
}

/* get Power Management Capability Structure register group size */
static uint8_t pt_pm_size_init(struct pt_dev *ptdev,
        struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset)
{
    if (!ptdev->power_mgmt)
        return grp_reg->grp_size;

    ptdev->pm_state = qemu_mallocz(sizeof(struct pt_pm_info));
    if (!ptdev->pm_state)
    {
        /* exit I/O emulator */
        PT_LOG("Error: Allocating pt_pm_info failed. I/O emulator exit.\n");
        exit(1);
    }

    /* set Power Management Capability base offset */
    ptdev->pm_state->pm_base = base_offset;

    /* find AER register and set AER Capability base offset */
    ptdev->pm_state->aer_base = find_ext_cap_offset(ptdev->pci_dev,
        (uint32_t)PCI_EXT_CAP_ID_AER);

    /* save AER register */
    if (ptdev->pm_state->aer_base)
        pt_aer_reg_save(ptdev);

    return grp_reg->grp_size;
}

/* get MSI Capability Structure register group size */
static uint8_t pt_msi_size_init(struct pt_dev *ptdev,
        struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset)
{
    PCIDevice *d = &ptdev->dev;
    uint16_t msg_ctrl = 0;
    uint8_t msi_size = 0xa;

    msg_ctrl = *((uint16_t*)(d->config + (base_offset + PCI_MSI_FLAGS)));

    /* check 64 bit address capable & Per-vector masking capable */
    if (msg_ctrl & PCI_MSI_FLAGS_64BIT)
        msi_size += 4;
    if (msg_ctrl & PCI_MSI_FLAGS_MASK_BIT)
        msi_size += 10;

    ptdev->msi = malloc(sizeof(struct pt_msi_info));
    if ( !ptdev->msi )
    {
        /* exit I/O emulator */
        PT_LOG("Error: Allocating pt_msi_info failed. I/O emulator exit.\n");
        exit(1);
    }
    memset(ptdev->msi, 0, sizeof(struct pt_msi_info));
    ptdev->msi->pirq = -1;

    return msi_size;
}

/* get MSI-X Capability Structure register group size */
static uint8_t pt_msix_size_init(struct pt_dev *ptdev,
        struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset)
{
    int ret = 0;

    ret = pt_msix_init(ptdev, base_offset);

    if (ret == -1)
    {
        /* exit I/O emulator */
        PT_LOG("Internal error: Invalid pt_msix_init return value[%d]. "
            "I/O emulator exit.\n", ret);
        exit(1);
    }

    return grp_reg->grp_size;
}

/* get Vendor Specific Capability Structure register group size */
static uint8_t pt_vendor_size_init(struct pt_dev *ptdev,
        struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset)
{
    return ptdev->dev.config[base_offset + 0x02];
}

/* get PCI Express Capability Structure register group size */
static uint8_t pt_pcie_size_init(struct pt_dev *ptdev,
        struct pt_reg_grp_info_tbl *grp_reg, uint32_t base_offset)
{
    PCIDevice *d = &ptdev->dev;
    uint16_t exp_flag = 0;
    uint16_t type = 0;
    uint16_t vers = 0;
    uint8_t pcie_size = 0;

    exp_flag = *((uint16_t*)(d->config + (base_offset + PCI_EXP_FLAGS)));
    type = (exp_flag & PCI_EXP_FLAGS_TYPE) >> 4;
    vers = (exp_flag & PCI_EXP_FLAGS_VERS);

    /* calculate size depend on capability version and device/port type */
    /* in case of PCI Express Base Specification Rev 1.x */
    if (vers == 1)
    {
        /* The PCI Express Capabilities, Device Capabilities, and Device
         * Status/Control registers are required for all PCI Express devices.
         * The Link Capabilities and Link Status/Control are required for all
         * Endpoints that are not Root Complex Integrated Endpoints. Endpoints
         * are not required to implement registers other than those listed
         * above and terminate the capability structure.
         */
        switch (type) {
        case PCI_EXP_TYPE_ENDPOINT:
        case PCI_EXP_TYPE_LEG_END:
            pcie_size = 0x14;
            break;
        case PCI_EXP_TYPE_ROOT_INT_EP:
            /* has no link */
            pcie_size = 0x0C;
            break;
        /* only EndPoint passthrough is supported */
        case PCI_EXP_TYPE_ROOT_PORT:
        case PCI_EXP_TYPE_UPSTREAM:
        case PCI_EXP_TYPE_DOWNSTREAM:
        case PCI_EXP_TYPE_PCI_BRIDGE:
        case PCI_EXP_TYPE_PCIE_BRIDGE:
        case PCI_EXP_TYPE_ROOT_EC:
        default:
            /* exit I/O emulator */
            PT_LOG("Internal error: Unsupported device/port type[%d]. "
                "I/O emulator exit.\n", type);
            exit(1);
        }
    }
    /* in case of PCI Express Base Specification Rev 2.0 */
    else if (vers == 2)
    {
        switch (type) {
        case PCI_EXP_TYPE_ENDPOINT:
        case PCI_EXP_TYPE_LEG_END:
        case PCI_EXP_TYPE_ROOT_INT_EP:
            /* For Functions that do not implement the registers,
             * these spaces must be hardwired to 0b.
             */
            pcie_size = 0x3C;
            break;
        /* only EndPoint passthrough is supported */
        case PCI_EXP_TYPE_ROOT_PORT:
        case PCI_EXP_TYPE_UPSTREAM:
        case PCI_EXP_TYPE_DOWNSTREAM:
        case PCI_EXP_TYPE_PCI_BRIDGE:
        case PCI_EXP_TYPE_PCIE_BRIDGE:
        case PCI_EXP_TYPE_ROOT_EC:
        default:
            /* exit I/O emulator */
            PT_LOG("Internal error: Unsupported device/port type[%d]. "
                "I/O emulator exit.\n", type);
            exit(1);
        }
    }
    else
    {
        /* exit I/O emulator */
        PT_LOG("Internal error: Unsupported capability version[%d]. "
            "I/O emulator exit.\n", vers);
        exit(1);
    }

    return pcie_size;
}

/* read PCI_HEADER_TYPE */
static uint32_t pt_header_type_reg_init(struct pt_dev *ptdev,
    struct pt_reg_info_tbl *reg, uint32_t real_offset)
{
    return reg->init_val | 0x80;
}

/* read byte size emulate register */
static int pt_byte_reg_read(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint8_t *value, uint8_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint8_t valid_emu_mask = 0;

    /* emulate byte register */
    valid_emu_mask = reg->emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, cfg_entry->data, ~valid_emu_mask);

    return 0;
}

/* read word size emulate register */
static int pt_word_reg_read(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint16_t *value, uint16_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint16_t valid_emu_mask = 0;

    /* emulate word register */
    valid_emu_mask = reg->emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, cfg_entry->data, ~valid_emu_mask);

    return 0;
}

/* read long size emulate register */
static int pt_long_reg_read(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t *value, uint32_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint32_t valid_emu_mask = 0;

    /* emulate long register */
    valid_emu_mask = reg->emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, cfg_entry->data, ~valid_emu_mask);

   return 0;
}

/* read Command register */
static int pt_cmd_reg_read(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint16_t *value, uint16_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint16_t valid_emu_mask = 0;
    uint16_t emu_mask = reg->emu_mask;

    if ( ptdev->is_virtfn )
        emu_mask |= PCI_COMMAND_MEMORY;
    if ( pt_is_iomul(ptdev) )
        emu_mask |= PCI_COMMAND_IO;

    /* emulate word register */
    valid_emu_mask = emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, cfg_entry->data, ~valid_emu_mask);

    return 0;
}

/* read BAR */
static int pt_bar_reg_read(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t *value, uint32_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint32_t valid_emu_mask = 0;
    uint32_t bar_emu_mask = 0;
    int index;

    /* get BAR index */
    index = pt_bar_offset_to_index(reg->offset);
    if (index < 0)
    {
        /* exit I/O emulator */
        PT_LOG("Internal error: Invalid BAR index[%d]. "
            "I/O emulator exit.\n", index);
        exit(1);
    }

    /* use fixed-up value from kernel sysfs */
    *value = ptdev->pci_dev->base_addr[index];

    /* set emulate mask depend on BAR flag */
    switch (ptdev->bases[index].bar_flag)
    {
    case PT_BAR_FLAG_MEM:
        bar_emu_mask = PT_BAR_MEM_EMU_MASK;
        break;
    case PT_BAR_FLAG_IO:
        bar_emu_mask = PT_BAR_IO_EMU_MASK;
        break;
    case PT_BAR_FLAG_UPPER:
        bar_emu_mask = PT_BAR_ALLF;
        break;
    default:
        break;
    }

    /* emulate BAR */
    valid_emu_mask = bar_emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, cfg_entry->data, ~valid_emu_mask);

   return 0;
}


/* read Power Management Control/Status register */
static int pt_pmcsr_reg_read(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint16_t *value, uint16_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint16_t valid_emu_mask = reg->emu_mask;

    if (!ptdev->power_mgmt)
        valid_emu_mask |= PCI_PM_CTRL_STATE_MASK | PCI_PM_CTRL_NO_SOFT_RESET;

    valid_emu_mask = valid_emu_mask & valid_mask ;
    *value = PT_MERGE_VALUE(*value, cfg_entry->data, ~valid_emu_mask);

    return 0;
}


/* write byte size emulate register */
static int pt_byte_reg_write(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint8_t *value, uint8_t dev_value, uint8_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint8_t writable_mask = 0;
    uint8_t throughable_mask = 0;

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);

    /* create value for writing to I/O device register */
    throughable_mask = ~reg->emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    return 0;
}

/* write word size emulate register */
static int pt_word_reg_write(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint16_t *value, uint16_t dev_value, uint16_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint16_t writable_mask = 0;
    uint16_t throughable_mask = 0;

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);

    /* create value for writing to I/O device register */
    throughable_mask = ~reg->emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    return 0;
}

/* write long size emulate register */
static int pt_long_reg_write(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t *value, uint32_t dev_value, uint32_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint32_t writable_mask = 0;
    uint32_t throughable_mask = 0;

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);

    /* create value for writing to I/O device register */
    throughable_mask = ~reg->emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    return 0;
}

/* write Command register */
static int pt_cmd_reg_write(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint16_t *value, uint16_t dev_value, uint16_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint16_t writable_mask = 0;
    uint16_t throughable_mask = 0;
    uint16_t wr_value = *value;
    uint16_t emu_mask = reg->emu_mask;

    if ( ptdev->is_virtfn )
        emu_mask |= PCI_COMMAND_MEMORY;
    if ( pt_is_iomul(ptdev) )
        emu_mask |= PCI_COMMAND_IO;

    /* modify emulate register */
    writable_mask = ~reg->ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);

    /* create value for writing to I/O device register */
    throughable_mask = ~emu_mask & valid_mask;

    if (*value & PCI_COMMAND_DISABLE_INTx)
    {
        if (ptdev->msi_trans_en)
            msi_set_enable(ptdev, 0);
        else
            throughable_mask |= PCI_COMMAND_DISABLE_INTx;
    }
    else
    {
        if (ptdev->msi_trans_en)
            msi_set_enable(ptdev, 1);
        else
            if (ptdev->machine_irq)
                throughable_mask |= PCI_COMMAND_DISABLE_INTx;
    }

    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    /* mapping BAR */
    pt_bar_mapping(ptdev, wr_value & PCI_COMMAND_IO,
                          wr_value & PCI_COMMAND_MEMORY);

#ifndef CONFIG_STUBDOM
    if ( pt_is_iomul(ptdev) )
    {
        *value &= ~PCI_COMMAND_IO;
        if (ioctl(ptdev->fd, PCI_IOMUL_DISABLE_IO))
            PT_LOG("error: %s: %s\n", __func__, strerror(errno));
    }
#endif
    return 0;
}

/* write BAR */
static int pt_bar_reg_write(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t *value, uint32_t dev_value, uint32_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    struct pt_reg_grp_tbl *reg_grp_entry = NULL;
    struct pt_reg_tbl *reg_entry = NULL;
    struct pt_region *base = NULL;
    PCIDevice *d = (PCIDevice *)&ptdev->dev;
    PCIIORegion *r;
    uint32_t writable_mask = 0;
    uint32_t throughable_mask = 0;
    uint32_t bar_emu_mask = 0;
    uint32_t bar_ro_mask = 0;
    uint32_t new_addr, last_addr;
    uint32_t prev_offset;
    uint32_t r_size = 0;
    int index = 0;

    /* get BAR index */
    index = pt_bar_offset_to_index(reg->offset);
    if (index < 0)
    {
        /* exit I/O emulator */
        PT_LOG("Internal error: Invalid BAR index[%d]. "
            "I/O emulator exit.\n", index);
        exit(1);
    }

    r = &d->io_regions[index];
    r_size = r->size;
    base = &ptdev->bases[index];
    /* align resource size (memory type only) */
    PT_GET_EMUL_SIZE(base->bar_flag, r_size);

    /* set emulate mask and read-only mask depend on BAR flag */
    switch (ptdev->bases[index].bar_flag)
    {
    case PT_BAR_FLAG_MEM:
        bar_emu_mask = PT_BAR_MEM_EMU_MASK;
        bar_ro_mask = PT_BAR_MEM_RO_MASK | (r_size - 1);
        break;
    case PT_BAR_FLAG_IO:
        bar_emu_mask = PT_BAR_IO_EMU_MASK;
        bar_ro_mask = PT_BAR_IO_RO_MASK | (r_size - 1);
        break;
    case PT_BAR_FLAG_UPPER:
        bar_emu_mask = PT_BAR_ALLF;
        bar_ro_mask = 0;    /* all upper 32bit are R/W */
        break;
    default:
        break;
    }

    /* modify emulate register */
    writable_mask = bar_emu_mask & ~bar_ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);

    /* check whether we need to update the virtual region address or not */
    switch (ptdev->bases[index].bar_flag)
    {
    case PT_BAR_FLAG_MEM:
        /* nothing to do */
        break;
    case PT_BAR_FLAG_IO:
        new_addr = cfg_entry->data;
        last_addr = new_addr + r_size - 1;
        /* check invalid address */
        if (last_addr <= new_addr || !new_addr || last_addr >= 0x10000)
        {
            /* check 64K range */
            if ((last_addr >= 0x10000) &&
                (cfg_entry->data != (PT_BAR_ALLF & ~bar_ro_mask)))
            {
                PT_LOG_DEV(d, "Warning: Guest attempt to set Base Address "
                    "over the 64KB. "
                    "[Offset:%02xh][Address:%08xh][Size:%08xh]\n",
                    reg->offset, new_addr, r_size);
            }
            /* just remove mapping */
            r->addr = -1;
            goto exit;
        }
        break;
    case PT_BAR_FLAG_UPPER:
        if (cfg_entry->data)
        {
            if (cfg_entry->data != (PT_BAR_ALLF & ~bar_ro_mask))
            {
                PT_LOG_DEV(d, "Warning: Guest attempt to set high MMIO Base Address. "
                    "Ignore mapping. "
                    "[Offset:%02xh][High Address:%08xh]\n",
                    reg->offset, cfg_entry->data);
            }
            /* clear lower address */
            d->io_regions[index-1].addr = -1;
        }
        else
        {
            /* find lower 32bit BAR */
            prev_offset = (reg->offset - 4);
            reg_grp_entry = pt_find_reg_grp(ptdev, prev_offset);
            if (reg_grp_entry)
            {
                reg_entry = pt_find_reg(reg_grp_entry, prev_offset);
                if (reg_entry)
                    /* restore lower address */
                    d->io_regions[index-1].addr = reg_entry->data;
                else
                    return -1;
            }
            else
                return -1;
        }

        /* never mapping the 'empty' upper region,
         * because we'll do it enough for the lower region.
         */
        r->addr = -1;
        goto exit;
    default:
        break;
    }

    /* update the corresponding virtual region address */
    /*
     * When guest code tries to get block size of mmio, it will write all "1"s
     * into pci bar register. In this case, cfg_entry->data == writable_mask.
     * Especially for devices with large mmio, the value of writable_mask
     * is likely to be a guest physical address that has been mapped to ram
     * rather than mmio. Remapping this value to mmio should be prevented.
     */

    if ( cfg_entry->data != writable_mask )
        r->addr = cfg_entry->data;

exit:
    /* create value for writing to I/O device register */
    throughable_mask = ~bar_emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    /* After BAR reg update, we need to remap BAR*/
    reg_grp_entry = pt_find_reg_grp(ptdev, PCI_COMMAND);
    if (reg_grp_entry)
    {
        reg_entry = pt_find_reg(reg_grp_entry, PCI_COMMAND);
        if (reg_entry)
            pt_bar_mapping_one(ptdev, index, reg_entry->data & PCI_COMMAND_IO,
                               reg_entry->data & PCI_COMMAND_MEMORY);
    }

    return 0;
}

/* write Exp ROM BAR */
static int pt_exp_rom_bar_reg_write(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t *value, uint32_t dev_value, uint32_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    struct pt_reg_grp_tbl *reg_grp_entry = NULL;
    struct pt_reg_tbl *reg_entry = NULL;
    struct pt_region *base = NULL;
    PCIDevice *d = (PCIDevice *)&ptdev->dev;
    PCIIORegion *r;
    uint32_t writable_mask = 0;
    uint32_t throughable_mask = 0;
    uint32_t r_size = 0;
    uint32_t bar_emu_mask = 0;
    uint32_t bar_ro_mask = 0;

    r = &d->io_regions[PCI_ROM_SLOT];
    r_size = r->size;
    base = &ptdev->bases[PCI_ROM_SLOT];
    /* align memory type resource size */
    PT_GET_EMUL_SIZE(base->bar_flag, r_size);

    /* set emulate mask and read-only mask */
    bar_emu_mask = reg->emu_mask;
    bar_ro_mask = (reg->ro_mask | (r_size - 1)) & ~PCI_ROM_ADDRESS_ENABLE;

    /* modify emulate register */
    writable_mask = ~bar_ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);

    /* update the corresponding virtual region address */
    /*
     * When guest code tries to get block size of mmio, it will write all "1"s
     * into pci bar register. In this case, cfg_entry->data == writable_mask.
     * Especially for devices with large mmio, the value of writable_mask
     * is likely to be a guest physical address that has been mapped to ram
     * rather than mmio. Remapping this value to mmio should be prevented.
     */

    if ( cfg_entry->data != writable_mask )
        r->addr = cfg_entry->data;

    /* create value for writing to I/O device register */
    throughable_mask = ~bar_emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    /* After BAR reg update, we need to remap BAR*/
    reg_grp_entry = pt_find_reg_grp(ptdev, PCI_COMMAND);
    if (reg_grp_entry)
    {
        reg_entry = pt_find_reg(reg_grp_entry, PCI_COMMAND);
        if (reg_entry)
            pt_bar_mapping_one(ptdev, PCI_ROM_SLOT,
                               reg_entry->data & PCI_COMMAND_IO,
                               reg_entry->data & PCI_COMMAND_MEMORY);
    }

    return 0;
}

/* write Power Management Control/Status register */
static int pt_pmcsr_reg_write(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint16_t *value, uint16_t dev_value, uint16_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    PCIDevice *d = &ptdev->dev;
    uint16_t emu_mask = reg->emu_mask;
    uint16_t writable_mask = 0;
    uint16_t throughable_mask = 0;
    struct pt_pm_info *pm_state = ptdev->pm_state;
    uint16_t read_val = 0;

    if (!ptdev->power_mgmt)
        emu_mask |= PCI_PM_CTRL_STATE_MASK | PCI_PM_CTRL_NO_SOFT_RESET;

    /* modify emulate register */
    writable_mask = emu_mask & ~reg->ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);

    /* create value for writing to I/O device register */
    throughable_mask = ~emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    if (!ptdev->power_mgmt)
        return 0;

    /* set I/O device power state */
    pm_state->cur_state = (dev_value & PCI_PM_CTRL_STATE_MASK);

    /* set Guest requested PowerState */
    pm_state->req_state = (*value & PCI_PM_CTRL_STATE_MASK);

    /* check power state transition or not */
    if (pm_state->cur_state == pm_state->req_state)
        /* not power state transition */
        return 0;

    /* check enable power state transition */
    if ((pm_state->req_state != 0) &&
        (pm_state->cur_state > pm_state->req_state))
    {
        PT_LOG_DEV(d, "Error: Invalid power transition. "
            "[requested state:%d][current state:%d]\n",
            pm_state->req_state, pm_state->cur_state);

        return 0;
    }

    /* check if this device supports the requested power state */
    if (((pm_state->req_state == 1) && !(pm_state->pmc_field & PCI_PM_CAP_D1))
        || ((pm_state->req_state == 2) &&
        !(pm_state->pmc_field & PCI_PM_CAP_D2)))
    {
        PT_LOG_DEV(d, "Error: Invalid power transition. "
            "[requested state:%d][current state:%d]\n",
            pm_state->req_state, pm_state->cur_state);

        return 0;
    }

    /* in case of transition related to D3hot, it's necessary to wait 10 ms.
     * But because writing to register will be performed later on actually,
     * don't start QEMUTimer right now, just alloc and init QEMUTimer here.
     */
    if ((pm_state->cur_state == 3) || (pm_state->req_state == 3))
    {
        if (pm_state->req_state == 0)
        {
            /* alloc and init QEMUTimer */
            if (!pm_state->no_soft_reset)
            {
                pm_state->pm_timer = qemu_new_timer(rt_clock,
                    pt_from_d3hot_to_d0_with_reset, ptdev);

                /* reset Interrupt and I/O resource mapping */
                pt_reset_interrupt_and_io_mapping(ptdev);
            }
            else
                pm_state->pm_timer = qemu_new_timer(rt_clock,
                    pt_default_power_transition, ptdev);
        }
        else
            /* alloc and init QEMUTimer */
            pm_state->pm_timer = qemu_new_timer(rt_clock,
                pt_default_power_transition, ptdev);

        /* set power state transition delay */
        pm_state->pm_delay = 10;

        /* power state transition flags on */
        pm_state->flags |= PT_FLAG_TRANSITING;
    }
    /* in case of transition related to D0, D1 and D2,
     * no need to use QEMUTimer.
     * So, we perfom writing to register here and then read it back.
     */
    else
    {
        /* write power state to I/O device register */
        pci_write_word(ptdev->pci_dev,
                        (pm_state->pm_base + PCI_PM_CTRL), *value);

        /* in case of transition related to D2,
         * it's necessary to wait 200 usec.
         * But because QEMUTimer do not support microsec unit right now,
         * so we do wait ourself here.
         */
        if ((pm_state->cur_state == 2) || (pm_state->req_state == 2))
            usleep(200);

        /* check power state */
        check_power_state(ptdev);

        /* recreate value for writing to I/O device register */
        *value = pci_read_word(ptdev->pci_dev,
                                (pm_state->pm_base + PCI_PM_CTRL));
    }

    return 0;
}

/* write Message Control register */
static int pt_msgctrl_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t dev_value, uint16_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint16_t writable_mask = 0;
    uint16_t throughable_mask = 0;
    uint16_t old_ctrl = cfg_entry->data;
    PCIDevice *pd = (PCIDevice *)ptdev;
    uint16_t val;

    /* Currently no support for multi-vector */
    if ((*value & PCI_MSI_FLAGS_QSIZE) != 0x0)
        PT_LOG("Warning: try to set more than 1 vector ctrl %x\n", *value);

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);
    /* update the msi_info too */
    ptdev->msi->flags |= cfg_entry->data &
        ~(MSI_FLAG_UNINIT | PT_MSI_MAPPED | PCI_MSI_FLAGS_ENABLE);

    /* create value for writing to I/O device register */
    val = *value;
    throughable_mask = ~reg->emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    /* update MSI */
    if (val & PCI_MSI_FLAGS_ENABLE)
    {
        /* setup MSI pirq for the first time */
        if (ptdev->msi->flags & MSI_FLAG_UNINIT)
        {
            if (ptdev->msi_trans_en) {
                PT_LOG("guest enabling MSI, disable MSI-INTx translation\n");
                pt_disable_msi_translate(ptdev);
            }
            /* Init physical one */
            PT_LOG("setup msi for dev %x\n", pd->devfn);
            if (pt_msi_setup(ptdev))
            {
                /* We do not broadcast the error to the framework code, so
                 * that MSI errors are contained in MSI emulation code and
                 * QEMU can go on running.
                 * Guest MSI would be actually not working.
                 */
                *value &= ~PCI_MSI_FLAGS_ENABLE;
                PT_LOG("Warning: Can not map MSI for dev %x\n", pd->devfn);
                return 0;
            }
            if (pt_msi_update(ptdev))
            {
                *value &= ~PCI_MSI_FLAGS_ENABLE;
                PT_LOG("Warning: Can not bind MSI for dev %x\n", pd->devfn);
                return 0;
            }
            ptdev->msi->flags &= ~MSI_FLAG_UNINIT;
            ptdev->msi->flags |= PT_MSI_MAPPED;
        }
        ptdev->msi->flags |= PCI_MSI_FLAGS_ENABLE;
    }
    else
        ptdev->msi->flags &= ~PCI_MSI_FLAGS_ENABLE;

    /* pass through MSI_ENABLE bit when no MSI-INTx translation */
    if (!ptdev->msi_trans_en) {
        *value &= ~PCI_MSI_FLAGS_ENABLE;
        *value |= val & PCI_MSI_FLAGS_ENABLE;
    }

    return 0;
}

/* write Message Address register */
static int pt_msgaddr32_reg_write(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t *value, uint32_t dev_value, uint32_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint32_t writable_mask = 0;
    uint32_t throughable_mask = 0;
    uint32_t old_addr = cfg_entry->data;

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);
    /* update the msi_info too */
    ptdev->msi->addr_lo = cfg_entry->data;

    /* create value for writing to I/O device register */
    throughable_mask = ~reg->emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    /* update MSI */
    if (cfg_entry->data != old_addr)
    {
        if (ptdev->msi->flags & PT_MSI_MAPPED)
            pt_msi_update(ptdev);
    }

    return 0;
}

/* write Message Upper Address register */
static int pt_msgaddr64_reg_write(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t *value, uint32_t dev_value, uint32_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint32_t writable_mask = 0;
    uint32_t throughable_mask = 0;
    uint32_t old_addr = cfg_entry->data;

    /* check whether the type is 64 bit or not */
    if (!(ptdev->msi->flags & PCI_MSI_FLAGS_64BIT))
    {
        /* exit I/O emulator */
        PT_LOG("Error: why comes to Upper Address without 64 bit support??\n");
        return -1;
    }

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);
    /* update the msi_info too */
    ptdev->msi->addr_hi = cfg_entry->data;

    /* create value for writing to I/O device register */
    throughable_mask = ~reg->emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    /* update MSI */
    if (cfg_entry->data != old_addr)
    {
        if (ptdev->msi->flags & PT_MSI_MAPPED)
            pt_msi_update(ptdev);
    }

    return 0;
}

/* this function will be called twice (for 32 bit and 64 bit type) */
/* write Message Data register */
static int pt_msgdata_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t dev_value, uint16_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint16_t writable_mask = 0;
    uint16_t throughable_mask = 0;
    uint16_t old_data = cfg_entry->data;
    uint32_t flags = ptdev->msi->flags;
    uint32_t offset = reg->offset;

    /* check the offset whether matches the type or not */
    if (!((offset == PCI_MSI_DATA_64) &&  (flags & PCI_MSI_FLAGS_64BIT)) &&
        !((offset == PCI_MSI_DATA_32) && !(flags & PCI_MSI_FLAGS_64BIT)))
    {
        /* exit I/O emulator */
        PT_LOG("Error: the offset is not match with the 32/64 bit type!!\n");
        return -1;
    }

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);
    /* update the msi_info too */
    ptdev->msi->data = cfg_entry->data;

    /* create value for writing to I/O device register */
    throughable_mask = ~reg->emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    /* update MSI */
    if (cfg_entry->data != old_data)
    {
        if (flags & PT_MSI_MAPPED)
            pt_msi_update(ptdev);
    }

    return 0;
}

/* write Message Control register for MSI-X */
static int pt_msixctrl_reg_write(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint16_t *value, uint16_t dev_value, uint16_t valid_mask)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint16_t writable_mask = 0;
    uint16_t throughable_mask = 0;
    uint16_t old_ctrl = cfg_entry->data;

    /* modify emulate register */
    writable_mask = reg->emu_mask & ~reg->ro_mask & valid_mask;
    cfg_entry->data = PT_MERGE_VALUE(*value, cfg_entry->data, writable_mask);

    /* create value for writing to I/O device register */
    throughable_mask = ~reg->emu_mask & valid_mask;
    *value = PT_MERGE_VALUE(*value, dev_value, throughable_mask);

    /* update MSI-X */
    if ((*value & PCI_MSIX_ENABLE) && !(*value & PCI_MSIX_MASK))
    {
        if (ptdev->msi_trans_en) {
            PT_LOG("guest enabling MSI-X, disable MSI-INTx translation\n");
            pt_disable_msi_translate(ptdev);
        }
        pt_msix_update(ptdev);
    }

    ptdev->msix->enabled = !!(*value & PCI_MSIX_ENABLE);

    return 0;
}

/* restore byte size emulate register */
static int pt_byte_reg_restore(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t real_offset, uint8_t dev_value, uint8_t *value)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    PCIDevice *d = &ptdev->dev;

    /* use I/O device register's value as restore value */
    *value = *(uint8_t *)(d->config + real_offset);

    /* create value for restoring to I/O device register */
    *value = PT_MERGE_VALUE(*value, dev_value, reg->emu_mask);

    return 0;
}

/* restore word size emulate register */
static int pt_word_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint16_t dev_value, uint16_t *value)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    PCIDevice *d = &ptdev->dev;

    /* use I/O device register's value as restore value */
    *value = *(uint16_t *)(d->config + real_offset);

    /* create value for restoring to I/O device register */
    *value = PT_MERGE_VALUE(*value, dev_value, reg->emu_mask);

    return 0;
}

/* restore long size emulate register */
static int pt_long_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint32_t dev_value, uint32_t *value)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    PCIDevice *d = &ptdev->dev;

    /* use I/O device register's value as restore value */
    *value = *(uint32_t *)(d->config + real_offset);

    /* create value for restoring to I/O device register */
    *value = PT_MERGE_VALUE(*value, dev_value, reg->emu_mask);

    return 0;
}

/* restore Command register */
static int pt_cmd_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint16_t dev_value, uint16_t *value)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    PCIDevice *d = &ptdev->dev;
    uint16_t restorable_mask = 0;

    /* use I/O device register's value as restore value */
    *value = *(uint16_t *)(d->config + real_offset);

    /* create value for restoring to I/O device register
     * but do not include Fast Back-to-Back Enable bit.
     */
    restorable_mask = reg->emu_mask & ~PCI_COMMAND_FAST_BACK;
    *value = PT_MERGE_VALUE(*value, dev_value, restorable_mask);
#ifndef CONFIG_STUBDOM
    if ( pt_is_iomul(ptdev) ) {
        *value &= ~PCI_COMMAND_IO;
        if (ioctl(ptdev->fd, PCI_IOMUL_DISABLE_IO))
            PT_LOG("error: %s: %s\n", __func__, strerror(errno));
    }
#endif

    if (!ptdev->machine_irq)
        *value |= PCI_COMMAND_DISABLE_INTx;
    else
        *value &= ~PCI_COMMAND_DISABLE_INTx;

    return 0;
}

/* restore BAR */
static int pt_bar_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint32_t dev_value, uint32_t *value)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;
    uint32_t bar_emu_mask = 0;
    int index = 0;

    /* get BAR index */
    index = pt_bar_offset_to_index(reg->offset);
    if (index < 0)
    {
        /* exit I/O emulator */
        PT_LOG("Internal error: Invalid BAR index[%d]. "
            "I/O emulator exit.\n", index);
        exit(1);
    }

    /* use value from kernel sysfs */
    if (ptdev->bases[index].bar_flag == PT_BAR_FLAG_UPPER)
        *value = ptdev->pci_dev->base_addr[index-1] >> 32;
    else
        *value = ptdev->pci_dev->base_addr[index];

    /* set emulate mask depend on BAR flag */
    switch (ptdev->bases[index].bar_flag)
    {
    case PT_BAR_FLAG_MEM:
        bar_emu_mask = PT_BAR_MEM_EMU_MASK;
        break;
    case PT_BAR_FLAG_IO:
        bar_emu_mask = PT_BAR_IO_EMU_MASK;
        break;
    case PT_BAR_FLAG_UPPER:
        bar_emu_mask = PT_BAR_ALLF;
        break;
    default:
        break;
    }

    /* create value for restoring to I/O device register */
    *value = PT_MERGE_VALUE(*value, dev_value, bar_emu_mask);

    return 0;
}

/* restore ROM BAR */
static int pt_exp_rom_bar_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint32_t dev_value, uint32_t *value)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;

    /* use value from kernel sysfs */
    *value = PT_MERGE_VALUE(ptdev->pci_dev->rom_base_addr, dev_value, 
                            reg->emu_mask);
    return 0;
}

/* restore Power Management Control/Status register */
static int pt_pmcsr_reg_restore(struct pt_dev *ptdev,
    struct pt_reg_tbl *cfg_entry,
    uint32_t real_offset, uint16_t dev_value, uint16_t *value)
{
    struct pt_reg_info_tbl *reg = cfg_entry->reg;

    /* create value for restoring to I/O device register
     * No need to restore, just clear PME Enable and PME Status bit
     * Note: register type of PME Status bit is RW1C, so clear by writing 1b
     */
    *value = (dev_value & ~PCI_PM_CTRL_PME_ENABLE) | PCI_PM_CTRL_PME_STATUS;

    return 0;
}

static int pt_intel_opregion_read(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t *value, uint32_t valid_mask)
{
    *value = igd_read_opregion(ptdev);
    return 0;
}

static int pt_intel_opregion_write(struct pt_dev *ptdev,
        struct pt_reg_tbl *cfg_entry,
        uint32_t *value, uint32_t dev_value, uint32_t valid_mask)
{
    igd_write_opregion(ptdev, *value);
    return 0;
}

static struct pt_dev * register_real_device(PCIBus *e_bus,
        const char *e_dev_name, int e_devfn, uint8_t r_bus, uint8_t r_dev,
        uint8_t r_func, uint32_t machine_irq, struct pci_access *pci_access,
        char *opt)
{
    int rc = -1, i;
    struct pt_dev *assigned_device = NULL;
    struct pci_dev *pci_dev;
    uint8_t e_device, e_intx;
    char *key, *val;
    int msi_translate, power_mgmt;

    PT_LOG("Assigning real physical device %02x:%02x.%x ...\n",
        r_bus, r_dev, r_func);

    /* Find real device structure */
    for (pci_dev = pci_access->devices; pci_dev != NULL;
         pci_dev = pci_dev->next)
    {
        if ((r_bus == pci_dev->bus) && (r_dev == pci_dev->dev)
            && (r_func == pci_dev->func))
            break;
    }
    if ( pci_dev == NULL )
    {
        PT_LOG("Error: couldn't locate device in libpci structures\n");
        return NULL;
    }
    pci_fill_info(pci_dev, PCI_FILL_IRQ | PCI_FILL_BASES | PCI_FILL_ROM_BASE | PCI_FILL_SIZES | PCI_FILL_IDENT | PCI_FILL_CLASS);
    pt_libpci_fixup(pci_dev);

    msi_translate = direct_pci_msitranslate;
    power_mgmt = direct_pci_power_mgmt;
    while (opt) {
        if (get_next_keyval(&opt, &key, &val)) {
            PT_LOG("Error: unrecognized PCI assignment option \"%s\"\n", opt);
            break;
        }

        if (strcmp(key, "msitranslate") == 0)
        {
            if (strcmp(val, "0") == 0 || strcmp(val, "no") == 0)
            {
                PT_LOG("Disable MSI translation via per device option\n");
                msi_translate = 0;
            }
            else if (strcmp(val, "1") == 0 || strcmp(val, "yes") == 0)
            {
                PT_LOG("Enable MSI translation via per device option\n");
                msi_translate = 1;
            }
            else
                PT_LOG("Error: unrecognized value for msitranslate=\n");
        }
        else if (strcmp(key, "power_mgmt") == 0)
        {
            if (strcmp(val, "0") == 0)
            {
                PT_LOG("Disable power management\n");
                power_mgmt = 0;
            }
            else if (strcmp(val, "1") == 0)
            {
                PT_LOG("Enable power management\n");
                power_mgmt = 1;
            }
            else
                PT_LOG("Error: unrecognized value for power_mgmt=\n");
        }
        else
            PT_LOG("Error: unrecognized PCI assignment option \"%s=%s\"\n", key, val);

    }


    /* Register device */
    assigned_device = (struct pt_dev *) pci_register_device(e_bus, e_dev_name,
                                sizeof(struct pt_dev), e_devfn,
                                pt_pci_read_config, pt_pci_write_config);
    if ( assigned_device == NULL )
    {
        PT_LOG("Error: couldn't register real device\n");
        return NULL;
    }

    dpci_infos.php_devs[e_devfn].pt_dev = assigned_device;

    assigned_device->pci_dev = pci_dev;
    assigned_device->msi_trans_cap = msi_translate;
    assigned_device->power_mgmt = power_mgmt;
    assigned_device->is_virtfn = pt_dev_is_virtfn(pci_dev);
    pt_iomul_init(assigned_device, r_bus, r_dev, r_func);

    /* Initialize virtualized PCI configuration (Extended 256 Bytes) */
    for ( i = 0; i < PCI_CONFIG_SIZE; i++ )
        assigned_device->dev.config[i] = pci_read_byte(pci_dev, i);

    /* Handle real device's MMIO/PIO BARs */
    pt_register_regions(assigned_device);

    /* Setup VGA bios for passthroughed gfx */
    if ( setup_vga_pt(assigned_device) < 0 )
    {
        PT_LOG("Setup VGA BIOS of passthroughed gfx failed!\n");
        return NULL;
    }


    /* reinitialize each config register to be emulated */
    rc = pt_config_init(assigned_device);
    if ( rc < 0 ) {
        return NULL;
    }

    /* Bind interrupt */
    if (!assigned_device->dev.config[PCI_INTERRUPT_PIN])
        goto out;

    if ( PT_MACHINE_IRQ_AUTO == machine_irq )
    {
        int pirq = -1;

        machine_irq = pci_dev->irq;
        rc = xc_physdev_map_pirq(xc_handle, domid, machine_irq, &pirq);

        if ( rc )
        {
            PT_LOG("Error: Mapping irq failed, rc = %d\n", rc);

            /* Disable PCI intx assertion (turn on bit10 of devctl) */
            pci_write_word(pci_dev, PCI_COMMAND,
                *(uint16_t *)(&assigned_device->dev.config[PCI_COMMAND])
                | PCI_COMMAND_DISABLE_INTx);
            machine_irq = 0;
            assigned_device->machine_irq = 0;
        }
        else
        {
            machine_irq = pirq;
            assigned_device->machine_irq = pirq;
            mapped_machine_irq[machine_irq]++;
        }
    }

    /* setup MSI-INTx translation if support */
    rc = pt_enable_msi_translate(assigned_device);

    /* bind machine_irq to device */
    if (rc < 0 && machine_irq != 0)
    {
        e_device = PCI_SLOT(assigned_device->dev.devfn);
        e_intx = pci_intx(assigned_device);

        rc = xc_domain_bind_pt_pci_irq(xc_handle, domid, machine_irq, 0,
                                       e_device, e_intx);
        if ( rc < 0 )
        {
            PT_LOG("Error: Binding of interrupt failed! rc=%d\n", rc);

            /* Disable PCI intx assertion (turn on bit10 of devctl) */
            pci_write_word(pci_dev, PCI_COMMAND,
                *(uint16_t *)(&assigned_device->dev.config[PCI_COMMAND])
                | PCI_COMMAND_DISABLE_INTx);
            mapped_machine_irq[machine_irq]--;

            if (mapped_machine_irq[machine_irq] == 0)
            {
                if (xc_physdev_unmap_pirq(xc_handle, domid, machine_irq))
                    PT_LOG("Error: Unmapping of interrupt failed! rc=%d\n",
                        rc);
            }
            assigned_device->machine_irq = 0;
        }
    }

out:
    PT_LOG("Real physical device %02x:%02x.%x registered successfuly!\n"
           "IRQ type = %s\n", r_bus, r_dev, r_func,
           assigned_device->msi_trans_en? "MSI-INTx":"INTx");

    return assigned_device;
}

static int unregister_real_device(int devfn)
{
    struct php_dev *php_dev;
    struct pci_dev *pci_dev;
    uint8_t e_device, e_intx;
    struct pt_dev *assigned_device = NULL;
    uint32_t machine_irq;
    uint32_t bdf = 0;
    int rc = -1;

    if ( test_pci_devfn(devfn) != 1 )
       return -1;

    php_dev = &dpci_infos.php_devs[devfn];
    assigned_device = php_dev->pt_dev;

    if ( !assigned_device )
        return -1;

    pci_dev = assigned_device->pci_dev;

    /* hide pci dev from qemu */
    pci_hide_device((PCIDevice*)assigned_device);

    /* Unbind interrupt */
    e_device = PCI_SLOT(assigned_device->dev.devfn);
    e_intx = pci_intx(assigned_device);
    machine_irq = assigned_device->machine_irq;

    if ( assigned_device->msi_trans_en == 0 && machine_irq ) {
        rc = xc_domain_unbind_pt_irq(xc_handle, domid, machine_irq, PT_IRQ_TYPE_PCI, 0,
                                       e_device, e_intx, 0);
        if ( rc < 0 )
        {
            PT_LOG("Error: Unbinding of interrupt failed! rc=%d\n", rc);
        }
    }

    if (assigned_device->msi)
        pt_msi_disable(assigned_device);
    if (assigned_device->msix)
        pt_msix_disable(assigned_device);

    if (machine_irq)
    {
        mapped_machine_irq[machine_irq]--;

        if (mapped_machine_irq[machine_irq] == 0)
        {
            rc = xc_physdev_unmap_pirq(xc_handle, domid, machine_irq);

            if (rc < 0)
                PT_LOG("Error: Unmaping of interrupt failed! rc=%d\n", rc);
        }
    }

    /* unregister real device's MMIO/PIO BARs */
    pt_unregister_regions(assigned_device);

    /* delete all emulated config registers */
    pt_config_delete(assigned_device);

    pt_iomul_free(assigned_device);

    /* mark this devfn as free */
    php_dev->valid = 0;
    php_dev->pt_dev = NULL;
    qemu_free(assigned_device);

    return 0;
}

int power_on_php_devfn(int devfn)
{
    struct php_dev *php_dev = &dpci_infos.php_devs[devfn];
    struct pt_dev *pt_dev;

    pci_access_init();

    pt_dev =
        register_real_device(dpci_infos.e_bus,
            "DIRECT PCI",
            devfn,
            php_dev->r_bus,
            php_dev->r_dev,
            php_dev->r_func,
            PT_MACHINE_IRQ_AUTO,
            dpci_infos.pci_access,
            php_dev->opt);

    php_dev->opt = NULL;

    return 0;

}

int power_off_php_devfn(int php_devfn)
{
    return unregister_real_device(php_devfn);
}

int pt_init(PCIBus *e_bus)
{
    memset(&dpci_infos, 0, sizeof(struct dpci_infos));
    dpci_infos.e_bus      = e_bus;

    return 0;
}
