/*
 * Copyright (c) 2006, Intel Corporation.
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
 * Copyright (C) Allen Kay <allen.m.kay@intel.com>
 * Copyright (C) Weidong Han <weidong.han@intel.com>
 */

#ifndef _VTD_EXTERN_H_
#define _VTD_EXTERN_H_

#include "dmar.h"
#include <xen/keyhandler.h>

extern bool_t rwbf_quirk;

void print_iommu_regs(struct acpi_drhd_unit *drhd);
void print_vtd_entries(struct iommu *iommu, int bus, int devfn, u64 gmfn);
extern struct keyhandler dump_iommu_info_keyhandler;

int enable_qinval(struct iommu *iommu);
void disable_qinval(struct iommu *iommu);
int enable_intremap(struct iommu *iommu, int eim);
void disable_intremap(struct iommu *iommu);

void iommu_flush_cache_entry(void *addr, unsigned int size);
void iommu_flush_cache_page(void *addr, unsigned long npages);
int iommu_alloc(struct acpi_drhd_unit *drhd);
void iommu_free(struct acpi_drhd_unit *drhd);

int queue_invalidate_context(struct iommu *iommu,
    u16 did, u16 source_id, u8 function_mask, u8 granu);
int queue_invalidate_iotlb(struct iommu *iommu,
    u8 granu, u8 dr, u8 dw, u16 did, u8 am, u8 ih, u64 addr);
int queue_invalidate_iec(struct iommu *iommu,
    u8 granu, u8 im, u16 iidx);
int invalidate_sync(struct iommu *iommu);
int iommu_flush_iec_global(struct iommu *iommu);
int iommu_flush_iec_index(struct iommu *iommu, u8 im, u16 iidx);
void clear_fault_bits(struct iommu *iommu);

struct iommu * ioapic_to_iommu(unsigned int apic_id);
struct acpi_drhd_unit * ioapic_to_drhd(unsigned int apic_id);
struct acpi_drhd_unit * iommu_to_drhd(struct iommu *iommu);
struct acpi_rhsa_unit * drhd_to_rhsa(struct acpi_drhd_unit *drhd);

#ifdef CONFIG_X86_64
struct acpi_drhd_unit * find_ats_dev_drhd(struct iommu *iommu);

int ats_device(const struct pci_dev *, const struct acpi_drhd_unit *);

int dev_invalidate_iotlb(struct iommu *iommu, u16 did,
                         u64 addr, unsigned int size_order, u64 type);
#else

static inline struct acpi_drhd_unit *find_ats_dev_drhd(struct iommu *iommu)
{
    return NULL;
}

static inline int ats_device(const struct pci_dev *pdev,
                             const struct acpi_drhd_unit *drhd)
{
    return 0;
}

static inline int dev_invalidate_iotlb(struct iommu *iommu, u16 did, u64 addr,
                                       unsigned int size_order, u64 type)
{
    return 0;
}
#endif

int qinval_device_iotlb(struct iommu *iommu,
                        u32 max_invs_pend, u16 sid, u16 size, u64 addr);

unsigned int get_cache_line_size(void);
void cacheline_flush(char *);
void flush_all_cache(void);

u64 alloc_pgtable_maddr(struct acpi_drhd_unit *drhd, unsigned long npages);
void free_pgtable_maddr(u64 maddr);
void *map_vtd_domain_page(u64 maddr);
void unmap_vtd_domain_page(void *va);
int domain_context_mapping_one(struct domain *domain, struct iommu *iommu,
                               u8 bus, u8 devfn);
int domain_context_unmap_one(struct domain *domain, struct iommu *iommu,
                             u8 bus, u8 devfn);

int is_igd_vt_enabled_quirk(void);
void platform_quirks_init(void);
void vtd_ops_preamble_quirk(struct iommu* iommu);
void vtd_ops_postamble_quirk(struct iommu* iommu);
void me_wifi_quirk(struct domain *domain, u8 bus, u8 devfn, int map);
void pci_vtd_quirk(struct pci_dev *pdev);
int platform_supports_intremap(void);
int platform_supports_x2apic(void);

#endif // _VTD_EXTERN_H_
