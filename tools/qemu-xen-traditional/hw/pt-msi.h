#ifndef _PT_MSI_H
#define _PT_MSI_H

#include "pci/pci.h"
#include "pass-through.h"

#define  PCI_CAP_ID_MSI     0x05    /* Message Signalled Interrupts */
#define  PCI_CAP_ID_MSIX    0x11    /* MSI-X */

#ifndef PCI_MSI_FLAGS

/* Message Signalled Interrupts registers */
#define PCI_MSI_FLAGS       2   /* Various flags */
#define  PCI_MSI_FLAGS_64BIT    0x80    /* 64-bit addresses allowed */
#define  PCI_MSI_FLAGS_QSIZE    0x70    /* Message queue size configured */
#define  PCI_MSI_FLAGS_QMASK    0x0e    /* Maximum queue size available */
#define  PCI_MSI_FLAGS_ENABLE   0x01    /* MSI feature enabled */
#define PCI_MSI_RFU         3   /* Rest of capability flags */
#define PCI_MSI_ADDRESS_LO  4   /* Lower 32 bits */
#define PCI_MSI_ADDRESS_HI  8   /* Upper 32 bits (if PCI_MSI_FLAGS_64BIT set) */
#define PCI_MSI_DATA_32     8   /* 16 bits of data for 32-bit devices */
#define PCI_MSI_DATA_64     12  /* 16 bits of data for 64-bit devices */

#endif

/* MSI-X */
#define  PCI_MSIX_ENABLE    0x8000
#define  PCI_MSIX_MASK      0x4000
#ifndef PCI_MSIX_TABSIZE
#define  PCI_MSIX_TABSIZE   0x03ff
#endif
#define PCI_MSIX_TABLE      4
#define PCI_MSIX_PBA        8
#define  PCI_MSIX_BIR       0x7

#define MSI_FLAG_UNINIT 0x1000
#define PT_MSI_MAPPED   0x2000

#define MSI_DATA_VECTOR_SHIFT          0
#define     MSI_DATA_VECTOR(v)         (((u8)v) << MSI_DATA_VECTOR_SHIFT)

#define MSI_DATA_DELIVERY_SHIFT        8
#define     MSI_DATA_DELIVERY_FIXED    (0 << MSI_DATA_DELIVERY_SHIFT)
#define     MSI_DATA_DELIVERY_LOWPRI   (1 << MSI_DATA_DELIVERY_SHIFT)

#define MSI_DATA_LEVEL_SHIFT           14
#define     MSI_DATA_LEVEL_DEASSERT    (0 << MSI_DATA_LEVEL_SHIFT)
#define     MSI_DATA_LEVEL_ASSERT      (1 << MSI_DATA_LEVEL_SHIFT)

#define MSI_DATA_TRIGGER_SHIFT         15
#define     MSI_DATA_TRIGGER_EDGE      (0 << MSI_DATA_TRIGGER_SHIFT)
#define     MSI_DATA_TRIGGER_LEVEL     (1 << MSI_DATA_TRIGGER_SHIFT)

/*
   + * Shift/mask fields for APIC-based bus address
   + */

#define MSI_ADDR_HEADER                0xfee00000
#define MSI_TARGET_CPU_SHIFT           12

#define MSI_ADDR_DESTID_MASK           0xfff0000f
#define     MSI_ADDR_DESTID_CPU(cpu)   ((cpu) << MSI_TARGET_CPU_SHIFT)

#define MSI_ADDR_DESTMODE_SHIFT        2
#define     MSI_ADDR_DESTMODE_PHYS     (0 << MSI_ADDR_DESTMODE_SHIFT)
#define        MSI_ADDR_DESTMODE_LOGIC (1 << MSI_ADDR_DESTMODE_SHIFT)

#define MSI_ADDR_REDIRECTION_SHIFT     3
#define     MSI_ADDR_REDIRECTION_CPU   (0 << MSI_ADDR_REDIRECTION_SHIFT)
#define     MSI_ADDR_REDIRECTION_LOWPRI (1 << MSI_ADDR_REDIRECTION_SHIFT)

#define AUTO_ASSIGN -1

/* shift count for gflags */
#define GFLAGS_SHIFT_DEST_ID        0
#define GFLAGS_SHIFT_RH             8
#define GFLAGS_SHIFT_DM             9
#define GLFAGS_SHIFT_DELIV_MODE     12
#define GLFAGS_SHIFT_TRG_MODE       15

void
msi_set_enable(struct pt_dev *dev, int en);

int
pt_msi_setup(struct pt_dev *dev);

uint32_t
__get_msi_gflags(uint32_t data, uint64_t addr);

int
pt_msi_update(struct pt_dev *d);

void
pt_msi_disable(struct pt_dev *dev);

int
pt_enable_msi_translate(struct pt_dev* dev);

void
pt_disable_msi_translate(struct pt_dev *dev);

int
pt_msix_update_remap(struct pt_dev *d, int bar_index);

int
pt_msix_update(struct pt_dev *dev);

void
pt_msix_disable(struct pt_dev *dev);

int
has_msix_mapping(struct pt_dev *dev, int bar_index);

int
pt_msix_init(struct pt_dev *dev, int pos);

void
pt_msix_delete(struct pt_dev *dev);

#endif
