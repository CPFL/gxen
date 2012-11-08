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
 */
#ifndef __PASSTHROUGH_H__
#define __PASSTHROUGH_H__

#include "hw.h"
#include "pci.h"
#include "pci/header.h"
#include "pci/pci.h"
#include "exec-all.h"
#include "sys-queue.h"
#include "qemu-timer.h"

/* Log acesss */
#define PT_LOGGING_ENABLED

/* Print errors even if logging is disabled */
#define PT_ERR(_f, _a...)   fprintf(logfile, "%s: " _f, __func__, ##_a)
#ifdef PT_LOGGING_ENABLED
#define PT_LOG(_f, _a...)   fprintf(logfile, "%s: " _f, __func__, ##_a)
#define PT_LOG_DEV(_dev, _f, _a...)   fprintf(logfile, "%s: [%02x:%02x:%01x] " _f, __func__,    \
                                              pci_bus_num((_dev)->bus),                         \
                                              PCI_SLOT((_dev)->devfn),                          \
                                              PCI_FUNC((_dev)->devfn), ##_a)
#else
#define PT_LOG(_f, _a...)
#define PT_LOG_DEV(_dev, _f, _a...)
#endif

/* Some compilation flags */
// #define PT_DEBUG_PCI_CONFIG_ACCESS

#define PT_MACHINE_IRQ_AUTO (0xFFFFFFFF)
#define PT_VIRT_DEVFN_AUTO  (-1)
#define PT_NR_IRQS          (256)

/* Misc PCI constants that should be moved to a separate library :) */
#define PCI_CONFIG_SIZE         (256)
#define PCI_EXP_DEVCAP_FLR      (1 << 28)
#define PCI_EXP_DEVCTL_FLR      (1 << 15)
#define PCI_BAR_ENTRIES         (6)

/* because the current version of libpci (2.2.0) doesn't define these ID,
 * so we define Capability ID here.
 */
#ifndef PCI_COMMAND_DISABLE_INTx
/* Disable INTx interrupts */
#define PCI_COMMAND_DISABLE_INTx 0x400
#endif

#ifndef PCI_CAP_ID_HOTPLUG
/* SHPC Capability List Item reg group */
#define PCI_CAP_ID_HOTPLUG      0x0C
#endif

#ifndef PCI_CAP_ID_SSVID
/* Subsystem ID and Subsystem Vendor ID Capability List Item reg group */
#define PCI_CAP_ID_SSVID        0x0D
#endif

#ifdef PCI_PM_CTRL_NO_SOFT_RESET
#undef PCI_PM_CTRL_NO_SOFT_RESET
#endif
/* No Soft Reset for D3hot->D0 */
#define PCI_PM_CTRL_NO_SOFT_RESET 0x0008

#ifndef PCI_MSI_FLAGS_MASK_BIT
/* interrupt masking & reporting supported */
#define PCI_MSI_FLAGS_MASK_BIT  0x0100
#endif

#ifndef PCI_EXP_TYPE_PCIE_BRIDGE
/* PCI/PCI-X to PCIE Bridge */
#define PCI_EXP_TYPE_PCIE_BRIDGE 0x8
#endif

#ifndef PCI_EXP_TYPE_ROOT_INT_EP
/* Root Complex Integrated Endpoint */
#define PCI_EXP_TYPE_ROOT_INT_EP 0x9
#endif

#ifndef PCI_EXP_TYPE_ROOT_EC
/* Root Complex Event Collector */
#define PCI_EXP_TYPE_ROOT_EC     0xa
#endif

#ifndef PCI_ERR_UNCOR_MASK
/* Uncorrectable Error Mask */
#define PCI_ERR_UNCOR_MASK      8
#endif

#ifndef PCI_ERR_UNCOR_SEVER
/* Uncorrectable Error Severity */
#define PCI_ERR_UNCOR_SEVER     12
#endif

#ifndef PCI_ERR_COR_MASK
/* Correctable Error Mask */
#define PCI_ERR_COR_MASK        20
#endif

#ifndef PCI_ERR_CAP
/* Advanced Error Capabilities */
#define PCI_ERR_CAP             24
#endif

#ifndef PCI_EXT_CAP_ID
/* Extended Capabilities (PCI-X 2.0 and PCI Express) */
#define PCI_EXT_CAP_ID(header)   (header & 0x0000ffff)
#endif

#ifndef PCI_EXT_CAP_NEXT
/* Extended Capabilities (PCI-X 2.0 and PCI Express) */
#define PCI_EXT_CAP_NEXT(header) ((header >> 20) & 0xffc)
#endif

/* power state transition */
#define PT_FLAG_TRANSITING 0x0001

#define PT_INVALID_REG          0xFFFFFFFF      /* invalid register value */
#define PT_BAR_ALLF             0xFFFFFFFF      /* BAR ALLF value */
#define PT_BAR_MEM_RO_MASK      0x0000000F      /* BAR ReadOnly mask(Memory) */
#define PT_BAR_MEM_EMU_MASK     0xFFFFFFF0      /* BAR emul mask(Memory) */
#define PT_BAR_IO_RO_MASK       0x00000003      /* BAR ReadOnly mask(I/O) */
#define PT_BAR_IO_EMU_MASK      0xFFFFFFFC      /* BAR emul mask(I/O) */
enum {
    PT_BAR_FLAG_MEM = 0,                        /* Memory type BAR */
    PT_BAR_FLAG_IO,                             /* I/O type BAR */
    PT_BAR_FLAG_UPPER,                          /* upper 64bit BAR */
    PT_BAR_FLAG_UNUSED,                         /* unused BAR */
};
enum {
    GRP_TYPE_HARDWIRED = 0,                     /* 0 Hardwired reg group */
    GRP_TYPE_EMU,                               /* emul reg group */
};

#define PT_GET_EMUL_SIZE(flag, r_size) do { \
    if (flag == PT_BAR_FLAG_MEM) {\
        r_size = (((r_size) + XC_PAGE_SIZE - 1) & ~(XC_PAGE_SIZE - 1)); \
    }\
} while(0)

#define PT_MERGE_VALUE(value, data, val_mask) \
    (((value) & (val_mask)) | ((data) & ~(val_mask)))

struct pt_region {
    /* Virtual phys base & size */
    uint32_t e_physbase;
    uint32_t e_size;
    /* Index of region in qemu */
    uint32_t memory_index;
    /* BAR flag */
    uint32_t bar_flag;
    /* Translation of the emulated address */
    union {
        uint64_t maddr;
        uint64_t pio_base;
        uint64_t u;
    } access;
};

struct pt_msi_info {
    uint32_t flags;
    uint32_t ctrl_offset; /* saved control offset */
    int pirq;          /* guest pirq corresponding */
    uint32_t addr_lo;  /* guest message address */
    uint32_t addr_hi;  /* guest message upper address */
    uint16_t data;     /* guest message data */
};

struct msix_entry_info {
    int pirq;          /* -1 means unmapped */
    int flags;         /* flags indicting whether MSI ADDR or DATA is updated */
    uint32_t io_mem[4];
};

struct pt_msix_info {
    uint32_t ctrl_offset;
    int enabled;
    int total_entries;
    int bar_index;
    uint64_t table_base;
    uint32_t table_off;
    uint32_t table_offset_adjust;	/* page align mmap */
    uint64_t mmio_base_addr;
    int mmio_index;
    void *phys_iomem_base;
    struct msix_entry_info msix_entry[0];
};

struct pt_pm_info {
    QEMUTimer *pm_timer;  /* QEMUTimer struct */
    int no_soft_reset;    /* No Soft Reset flags */
    uint16_t flags;       /* power state transition flags */
    uint16_t pmc_field;   /* Power Management Capabilities field */
    int pm_delay;         /* power state transition delay */
    uint16_t cur_state;   /* current power state */
    uint16_t req_state;   /* requested power state */
    uint32_t pm_base;     /* Power Management Capability reg base offset */
    uint32_t aer_base;    /* AER Capability reg base offset */
};

/*
    This structure holds the context of the mapping functions
    and data that is relevant for qemu device management.
*/
struct pt_dev {
    PCIDevice dev;
    struct pci_dev *pci_dev;                    /* libpci struct */
    struct pt_region bases[PCI_NUM_REGIONS];    /* Access regions */
    LIST_HEAD (reg_grp_tbl_listhead, pt_reg_grp_tbl) reg_grp_tbl_head;
                                                /* emul reg group list */
    struct pt_msi_info *msi;                    /* MSI virtualization */
    struct pt_msix_info *msix;                  /* MSI-X virtualization */
    int machine_irq;                            /* saved pirq */
    /* Physical MSI to guest INTx translation when possible */
    int msi_trans_cap;
    unsigned msi_trans_en:1;
    unsigned power_mgmt:1;
    struct pt_pm_info *pm_state;                /* PM virtualization */
    unsigned is_virtfn:1;

    /* io port multiplexing */
#define PCI_IOMUL_INVALID_FD    (-1)
    int fd;
    unsigned io_enable:1;
};

static inline int pt_is_iomul(struct pt_dev *dev)
{
    return (dev->fd != PCI_IOMUL_INVALID_FD);
}

/* Used for formatting PCI BDF into cf8 format */
struct pci_config_cf8 {
    union {
        unsigned int value;
        struct {
            unsigned int reserved1:2;
            unsigned int reg:6;
            unsigned int func:3;
            unsigned int dev:5;
            unsigned int bus:8;
            unsigned int reserved2:7;
            unsigned int enable:1;
        };
    };
};

/* emul reg group management table */
struct pt_reg_grp_tbl {
    /* emul reg group list */
    LIST_ENTRY (pt_reg_grp_tbl) entries;
    /* emul reg group info table */
    struct pt_reg_grp_info_tbl *reg_grp;
    /* emul reg group base offset */
    uint32_t base_offset;
    /* emul reg group size */
    uint8_t size;
    /* emul reg management table list */
    LIST_HEAD (reg_tbl_listhead, pt_reg_tbl) reg_tbl_head;
};

/* emul reg group size initialize method */
typedef uint8_t (*pt_reg_size_init) (struct pt_dev *ptdev,
                                     struct pt_reg_grp_info_tbl *grp_reg,
                                     uint32_t base_offset);
/* emul reg group infomation table */
struct pt_reg_grp_info_tbl {
    /* emul reg group ID */
    uint8_t grp_id;
    /* emul reg group type */
    uint8_t grp_type;
    /* emul reg group size */
    uint8_t grp_size;
    /* emul reg get size method */
    pt_reg_size_init size_init;
    /* emul reg info table */
    struct pt_reg_info_tbl *emu_reg_tbl;
};

/* emul reg management table */
struct pt_reg_tbl {
    /* emul reg table list */
    LIST_ENTRY (pt_reg_tbl) entries;
    /* emul reg info table */
    struct pt_reg_info_tbl *reg;
    /* emul reg value */
    uint32_t data;
};

/* emul reg initialize method */
typedef uint32_t (*conf_reg_init) (struct pt_dev *ptdev,
                                   struct pt_reg_info_tbl *reg,
                                   uint32_t real_offset);
/* emul reg long write method */
typedef int (*conf_dword_write) (struct pt_dev *ptdev,
                                 struct pt_reg_tbl *cfg_entry,
                                 uint32_t *value,
                                 uint32_t dev_value,
                                 uint32_t valid_mask);
/* emul reg word write method */
typedef int (*conf_word_write) (struct pt_dev *ptdev,
                                struct pt_reg_tbl *cfg_entry,
                                uint16_t *value,
                                uint16_t dev_value,
                                uint16_t valid_mask);
/* emul reg byte write method */
typedef int (*conf_byte_write) (struct pt_dev *ptdev,
                                struct pt_reg_tbl *cfg_entry,
                                uint8_t *value,
                                uint8_t dev_value,
                                uint8_t valid_mask);
/* emul reg long read methods */
typedef int (*conf_dword_read) (struct pt_dev *ptdev,
                                struct pt_reg_tbl *cfg_entry,
                                uint32_t *value,
                                uint32_t valid_mask);
/* emul reg word read method */
typedef int (*conf_word_read) (struct pt_dev *ptdev,
                               struct pt_reg_tbl *cfg_entry,
                               uint16_t *value,
                               uint16_t valid_mask);
/* emul reg byte read method */
typedef int (*conf_byte_read) (struct pt_dev *ptdev,
                               struct pt_reg_tbl *cfg_entry,
                               uint8_t *value,
                               uint8_t valid_mask);
/* emul reg long restore method */
typedef int (*conf_dword_restore) (struct pt_dev *ptdev,
                                   struct pt_reg_tbl *cfg_entry,
                                   uint32_t real_offset,
                                   uint32_t dev_value,
                                   uint32_t *value);
/* emul reg word restore method */
typedef int (*conf_word_restore) (struct pt_dev *ptdev,
                                  struct pt_reg_tbl *cfg_entry,
                                  uint32_t real_offset,
                                  uint16_t dev_value,
                                  uint16_t *value);
/* emul reg byte restore method */
typedef int (*conf_byte_restore) (struct pt_dev *ptdev,
                                  struct pt_reg_tbl *cfg_entry,
                                  uint32_t real_offset,
                                  uint8_t dev_value,
                                  uint8_t *value);

/* emul reg infomation table */
struct pt_reg_info_tbl {
    /* reg relative offset */
    uint32_t offset;
    /* reg size */
    uint32_t size;
    /* reg initial value */
    uint32_t init_val;
    /* reg read only field mask (ON:RO/ROS, OFF:other) */
    uint32_t ro_mask;
    /* reg emulate field mask (ON:emu, OFF:passthrough) */
    uint32_t emu_mask;
    /* no write back allowed */
    uint32_t no_wb;
    /* emul reg initialize method */
    conf_reg_init init;
    union {
        struct {
            /* emul reg long write method */
            conf_dword_write write;
            /* emul reg long read method */
            conf_dword_read read;
            /* emul reg long restore method */
            conf_dword_restore restore;
        } dw;
        struct {
            /* emul reg word write method */
            conf_word_write write;
            /* emul reg word read method */
            conf_word_read read;
            /* emul reg word restore method */
            conf_word_restore restore;
        } w;
        struct {
            /* emul reg byte write method */
            conf_byte_write write;
            /* emul reg byte read method */
            conf_byte_read read;
            /* emul reg byte restore method */
            conf_byte_restore restore;
        } b;
    } u;
};

static inline pciaddr_t pt_pci_base_addr(pciaddr_t base)
{
    if ( base & PCI_ADDRESS_SPACE_IO )
        return base & PCI_ADDR_IO_MASK;

    return base & PCI_ADDR_MEM_MASK;
}

uint8_t pci_intx(struct pt_dev *ptdev);
struct pci_dev *pt_pci_get_dev(int bus, int dev, int func);
u32 pt_pci_host_read(struct pci_dev *pci_dev, u32 addr, int len);
int pt_pci_host_write(struct pci_dev *pci_dev, u32 addr, u32 val, int len);
void intel_pch_init(PCIBus *bus);
int register_vga_regions(struct pt_dev *real_device);
int unregister_vga_regions(struct pt_dev *real_device);
int setup_vga_pt(struct pt_dev *real_device);
PCIBus *intel_pci_bridge_init(PCIBus *bus, int devfn, uint16_t vid,
           uint16_t did, const char *name, uint16_t revision);
void igd_pci_write(PCIDevice *pci_dev, uint32_t config_addr, uint32_t val, int len);
uint32_t igd_pci_read(PCIDevice *pci_dev, uint32_t config_addr, int len);
uint32_t igd_read_opregion(struct pt_dev *pci_dev);
void igd_write_opregion(struct pt_dev *real_dev, uint32_t val);

#endif /* __PASSTHROUGH_H__ */

