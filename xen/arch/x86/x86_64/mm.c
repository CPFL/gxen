/******************************************************************************
 * arch/x86/x86_64/mm.c
 * 
 * Modifications to Linux original are copyright (c) 2004, K A Fraser tr This 
 * program is free software; you can redistribute it and/or modify it under 
 * the terms of the GNU General Public License as published by the Free 
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT 
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along 
 * with this program; if not, write to the Free Software Foundation, Inc., 59 
 * Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <xen/config.h>
#include <xen/lib.h>
#include <xen/init.h>
#include <xen/mm.h>
#include <xen/sched.h>
#include <xen/numa.h>
#include <xen/nodemask.h>
#include <xen/guest_access.h>
#include <asm/current.h>
#include <asm/asm_defns.h>
#include <asm/page.h>
#include <asm/flushtlb.h>
#include <asm/fixmap.h>
#include <asm/hypercall.h>
#include <asm/msr.h>
#include <asm/setup.h>
#include <asm/numa.h>
#include <asm/mem_event.h>
#include <asm/mem_sharing.h>
#include <public/memory.h>

/* Parameters for PFN/MADDR compression. */
unsigned long __read_mostly max_pdx;
unsigned long __read_mostly pfn_pdx_bottom_mask = ~0UL;
unsigned long __read_mostly ma_va_bottom_mask = ~0UL;
unsigned long __read_mostly pfn_top_mask = 0;
unsigned long __read_mostly ma_top_mask = 0;
unsigned long __read_mostly pfn_hole_mask = 0;
unsigned int __read_mostly pfn_pdx_hole_shift = 0;

unsigned int __read_mostly m2p_compat_vstart = __HYPERVISOR_COMPAT_VIRT_START;

/* Top-level master (and idle-domain) page directory. */
l4_pgentry_t __attribute__ ((__section__ (".bss.page_aligned")))
    idle_pg_table[L4_PAGETABLE_ENTRIES];

/* Enough page directories to map bottom 4GB of the memory map. */
l3_pgentry_t __attribute__ ((__section__ (".bss.page_aligned")))
    l3_identmap[L3_PAGETABLE_ENTRIES];
l2_pgentry_t __attribute__ ((__section__ (".bss.page_aligned")))
    l2_identmap[4*L2_PAGETABLE_ENTRIES];

/* Enough page directories to map the Xen text and static data. */
l3_pgentry_t __attribute__ ((__section__ (".bss.page_aligned")))
    l3_xenmap[L3_PAGETABLE_ENTRIES];
l2_pgentry_t __attribute__ ((__section__ (".bss.page_aligned")))
    l2_xenmap[L2_PAGETABLE_ENTRIES];

/* Enough page directories to map into the bottom 1GB. */
l3_pgentry_t __attribute__ ((__section__ (".bss.page_aligned")))
    l3_bootmap[L3_PAGETABLE_ENTRIES];
l2_pgentry_t __attribute__ ((__section__ (".bss.page_aligned")))
    l2_bootmap[L2_PAGETABLE_ENTRIES];

l2_pgentry_t *compat_idle_pg_table_l2;

int __mfn_valid(unsigned long mfn)
{
    return likely(mfn < max_page) &&
           likely(!(mfn & pfn_hole_mask)) &&
           likely(test_bit(pfn_to_pdx(mfn) / PDX_GROUP_COUNT,
                           pdx_group_valid));
}

void *alloc_xen_pagetable(void)
{
    unsigned long mfn;

    if ( system_state != SYS_STATE_early_boot )
    {
        struct page_info *pg = alloc_domheap_page(NULL, 0);

        BUG_ON(!dom0 && !pg);
        return pg ? page_to_virt(pg) : NULL;
    }

    mfn = alloc_boot_pages(1, 1);
    return mfn_to_virt(mfn);
}

l3_pgentry_t *virt_to_xen_l3e(unsigned long v)
{
    l4_pgentry_t *pl4e;

    pl4e = &idle_pg_table[l4_table_offset(v)];
    if ( !(l4e_get_flags(*pl4e) & _PAGE_PRESENT) )
    {
        l3_pgentry_t *pl3e = alloc_xen_pagetable();

        if ( !pl3e )
            return NULL;
        clear_page(pl3e);
        l4e_write(pl4e, l4e_from_paddr(__pa(pl3e), __PAGE_HYPERVISOR));
    }
    
    return l4e_to_l3e(*pl4e) + l3_table_offset(v);
}

l2_pgentry_t *virt_to_xen_l2e(unsigned long v)
{
    l3_pgentry_t *pl3e;

    pl3e = virt_to_xen_l3e(v);
    if ( !pl3e )
        return NULL;

    if ( !(l3e_get_flags(*pl3e) & _PAGE_PRESENT) )
    {
        l2_pgentry_t *pl2e = alloc_xen_pagetable();

        if ( !pl2e )
            return NULL;
        clear_page(pl2e);
        l3e_write(pl3e, l3e_from_paddr(__pa(pl2e), __PAGE_HYPERVISOR));
    }

    BUG_ON(l3e_get_flags(*pl3e) & _PAGE_PSE);
    return l3e_to_l2e(*pl3e) + l2_table_offset(v);
}

void *do_page_walk(struct vcpu *v, unsigned long addr)
{
    unsigned long mfn = pagetable_get_pfn(v->arch.guest_table);
    l4_pgentry_t l4e, *l4t;
    l3_pgentry_t l3e, *l3t;
    l2_pgentry_t l2e, *l2t;
    l1_pgentry_t l1e, *l1t;

    if ( is_hvm_vcpu(v) )
        return NULL;

    l4t = mfn_to_virt(mfn);
    l4e = l4t[l4_table_offset(addr)];
    mfn = l4e_get_pfn(l4e);
    if ( !(l4e_get_flags(l4e) & _PAGE_PRESENT) )
        return NULL;

    l3t = mfn_to_virt(mfn);
    l3e = l3t[l3_table_offset(addr)];
    mfn = l3e_get_pfn(l3e);
    if ( !(l3e_get_flags(l3e) & _PAGE_PRESENT) || !mfn_valid(mfn) )
        return NULL;
    if ( (l3e_get_flags(l3e) & _PAGE_PSE) )
        return mfn_to_virt(mfn) + (addr & ((1UL << L3_PAGETABLE_SHIFT) - 1));

    l2t = mfn_to_virt(mfn);
    l2e = l2t[l2_table_offset(addr)];
    mfn = l2e_get_pfn(l2e);
    if ( !(l2e_get_flags(l2e) & _PAGE_PRESENT) || !mfn_valid(mfn) )
        return NULL;
    if ( (l2e_get_flags(l2e) & _PAGE_PSE) )
        return mfn_to_virt(mfn) + (addr & ((1UL << L2_PAGETABLE_SHIFT) - 1));

    l1t = mfn_to_virt(mfn);
    l1e = l1t[l1_table_offset(addr)];
    mfn = l1e_get_pfn(l1e);
    if ( !(l1e_get_flags(l1e) & _PAGE_PRESENT) || !mfn_valid(mfn) )
        return NULL;

    return mfn_to_virt(mfn) + (addr & ~PAGE_MASK);
}

void __init pfn_pdx_hole_setup(unsigned long mask)
{
    unsigned int i, j, bottom_shift = 0, hole_shift = 0;

    /*
     * We skip the first MAX_ORDER bits, as we never want to compress them.
     * This guarantees that page-pointer arithmetic remains valid within
     * contiguous aligned ranges of 2^MAX_ORDER pages. Among others, our
     * buddy allocator relies on this assumption.
     */
    for ( j = MAX_ORDER-1; ; )
    {
        i = find_next_zero_bit(&mask, BITS_PER_LONG, j);
        j = find_next_bit(&mask, BITS_PER_LONG, i);
        if ( j >= BITS_PER_LONG )
            break;
        if ( j - i > hole_shift )
        {
            hole_shift = j - i;
            bottom_shift = i;
        }
    }
    if ( !hole_shift )
        return;

    printk(KERN_INFO "PFN compression on bits %u...%u\n",
           bottom_shift, bottom_shift + hole_shift - 1);

    pfn_pdx_hole_shift  = hole_shift;
    pfn_pdx_bottom_mask = (1UL << bottom_shift) - 1;
    ma_va_bottom_mask   = (PAGE_SIZE << bottom_shift) - 1;
    pfn_hole_mask       = ((1UL << hole_shift) - 1) << bottom_shift;
    pfn_top_mask        = ~(pfn_pdx_bottom_mask | pfn_hole_mask);
    ma_top_mask         = pfn_top_mask << PAGE_SHIFT;
}

/*
 * Allocate page table pages for m2p table
 */
struct mem_hotadd_info
{
    unsigned long spfn;
    unsigned long epfn;
    unsigned long cur;
};

int hotadd_mem_valid(unsigned long pfn, struct mem_hotadd_info *info)
{
    return (pfn < info->epfn && pfn >= info->spfn);
}

static unsigned long alloc_hotadd_mfn(struct mem_hotadd_info *info)
{
    unsigned mfn;

    ASSERT((info->cur + ( 1UL << PAGETABLE_ORDER) < info->epfn) &&
            info->cur >= info->spfn);

    mfn = info->cur;
    info->cur += (1UL << PAGETABLE_ORDER);
    return mfn;
}

#define M2P_NO_MAPPED   0
#define M2P_2M_MAPPED   1
#define M2P_1G_MAPPED   2
static int m2p_mapped(unsigned long spfn)
{
    unsigned long va;
    l3_pgentry_t *l3_ro_mpt;
    l2_pgentry_t *l2_ro_mpt;

    va = RO_MPT_VIRT_START + spfn * sizeof(*machine_to_phys_mapping);
    l3_ro_mpt = l4e_to_l3e(idle_pg_table[l4_table_offset(va)]);

    switch ( l3e_get_flags(l3_ro_mpt[l3_table_offset(va)]) &
             (_PAGE_PRESENT |_PAGE_PSE))
    {
        case _PAGE_PSE|_PAGE_PRESENT:
            return M2P_1G_MAPPED;
            break;
        /* Check for next level */
        case _PAGE_PRESENT:
            break;
        default:
            return M2P_NO_MAPPED;
            break;
    }
    l2_ro_mpt = l3e_to_l2e(l3_ro_mpt[l3_table_offset(va)]);

    if (l2e_get_flags(l2_ro_mpt[l2_table_offset(va)]) & _PAGE_PRESENT)
        return M2P_2M_MAPPED;

    return M2P_NO_MAPPED;
}

int share_hotadd_m2p_table(struct mem_hotadd_info *info)
{
    unsigned long i, n, v, m2p_start_mfn = 0;
    l3_pgentry_t l3e;
    l2_pgentry_t l2e;

    /* M2P table is mappable read-only by privileged domains. */
    for ( v  = RDWR_MPT_VIRT_START;
          v != RDWR_MPT_VIRT_END;
          v += n << PAGE_SHIFT )
    {
        n = L2_PAGETABLE_ENTRIES * L1_PAGETABLE_ENTRIES;
        l3e = l4e_to_l3e(idle_pg_table[l4_table_offset(v)])[
            l3_table_offset(v)];
        if ( !(l3e_get_flags(l3e) & _PAGE_PRESENT) )
            continue;
        if ( !(l3e_get_flags(l3e) & _PAGE_PSE) )
        {
            n = L1_PAGETABLE_ENTRIES;
            l2e = l3e_to_l2e(l3e)[l2_table_offset(v)];
            if ( !(l2e_get_flags(l2e) & _PAGE_PRESENT) )
                continue;
            m2p_start_mfn = l2e_get_pfn(l2e);
        }
        else
            continue;

        for ( i = 0; i < n; i++ )
        {
            struct page_info *page = mfn_to_page(m2p_start_mfn + i);
            if (hotadd_mem_valid(m2p_start_mfn + i, info))
                share_xen_page_with_privileged_guests(page, XENSHARE_readonly);
        }
    }

    for ( v  = RDWR_COMPAT_MPT_VIRT_START;
          v != RDWR_COMPAT_MPT_VIRT_END;
          v += 1 << L2_PAGETABLE_SHIFT )
    {
        l3e = l4e_to_l3e(idle_pg_table[l4_table_offset(v)])[
            l3_table_offset(v)];
        if ( !(l3e_get_flags(l3e) & _PAGE_PRESENT) )
            continue;
        l2e = l3e_to_l2e(l3e)[l2_table_offset(v)];
        if ( !(l2e_get_flags(l2e) & _PAGE_PRESENT) )
            continue;
        m2p_start_mfn = l2e_get_pfn(l2e);

        for ( i = 0; i < L1_PAGETABLE_ENTRIES; i++ )
        {
            struct page_info *page = mfn_to_page(m2p_start_mfn + i);
            if (hotadd_mem_valid(m2p_start_mfn + i, info))
                share_xen_page_with_privileged_guests(page, XENSHARE_readonly);
        }
    }
    return 0;
}

static void destroy_compat_m2p_mapping(struct mem_hotadd_info *info)
{
    unsigned long i, va, rwva, pt_pfn;
    unsigned long smap = info->spfn, emap = info->spfn;

    l3_pgentry_t *l3_ro_mpt;
    l2_pgentry_t *l2_ro_mpt;

    if ( smap > ((RDWR_COMPAT_MPT_VIRT_END - RDWR_COMPAT_MPT_VIRT_START) >> 2) )
        return;

    if ( emap > ((RDWR_COMPAT_MPT_VIRT_END - RDWR_COMPAT_MPT_VIRT_START) >> 2) )
        emap = (RDWR_COMPAT_MPT_VIRT_END - RDWR_COMPAT_MPT_VIRT_START) >> 2;

    l3_ro_mpt = l4e_to_l3e(idle_pg_table[l4_table_offset(HIRO_COMPAT_MPT_VIRT_START)]);

    ASSERT(l3e_get_flags(l3_ro_mpt[l3_table_offset(HIRO_COMPAT_MPT_VIRT_START)]) & _PAGE_PRESENT);

    l2_ro_mpt = l3e_to_l2e(l3_ro_mpt[l3_table_offset(HIRO_COMPAT_MPT_VIRT_START)]);

    for ( i = smap; i < emap; )
    {
        va = HIRO_COMPAT_MPT_VIRT_START +
              i * sizeof(*compat_machine_to_phys_mapping);
        rwva = RDWR_COMPAT_MPT_VIRT_START +
             i * sizeof(*compat_machine_to_phys_mapping);
        if ( l2e_get_flags(l2_ro_mpt[l2_table_offset(va)]) & _PAGE_PRESENT )
        {
            pt_pfn = l2e_get_pfn(l2_ro_mpt[l2_table_offset(va)]);
            if ( hotadd_mem_valid(pt_pfn, info) )
            {
                destroy_xen_mappings(rwva, rwva +
                        (1UL << L2_PAGETABLE_SHIFT));
                l2e_write(&l2_ro_mpt[l2_table_offset(va)], l2e_empty());
            }
        }

        i += 1UL < (L2_PAGETABLE_SHIFT - 2);
    }

    return;
}

void destroy_m2p_mapping(struct mem_hotadd_info *info)
{
    l3_pgentry_t *l3_ro_mpt;
    unsigned long i, va, rwva;
    unsigned long smap = info->spfn, emap = info->epfn;

    l3_ro_mpt = l4e_to_l3e(idle_pg_table[l4_table_offset(RO_MPT_VIRT_START)]);

    /*
     * No need to clean m2p structure existing before the hotplug
     */
    for (i = smap; i < emap;)
    {
        unsigned long pt_pfn;
        l2_pgentry_t *l2_ro_mpt;

        va = RO_MPT_VIRT_START + i * sizeof(*machine_to_phys_mapping);
        rwva = RDWR_MPT_VIRT_START + i * sizeof(*machine_to_phys_mapping);

        /* 1G mapping should not be created by mem hotadd */
        if (!(l3e_get_flags(l3_ro_mpt[l3_table_offset(va)]) & _PAGE_PRESENT) ||
            (l3e_get_flags(l3_ro_mpt[l3_table_offset(va)]) & _PAGE_PSE))
        {
            i = ( i & ~((1UL << (L3_PAGETABLE_SHIFT - 3)) - 1)) +
                (1UL << (L3_PAGETABLE_SHIFT - 3) );
            continue;
        }

        l2_ro_mpt = l3e_to_l2e(l3_ro_mpt[l3_table_offset(va)]);
        if (!(l2e_get_flags(l2_ro_mpt[l2_table_offset(va)]) & _PAGE_PRESENT))
        {
            i = ( i & ~((1UL << (L2_PAGETABLE_SHIFT - 3)) - 1)) +
                    (1UL << (L2_PAGETABLE_SHIFT - 3)) ;
            continue;
        }

        pt_pfn = l2e_get_pfn(l2_ro_mpt[l2_table_offset(va)]);
        if ( hotadd_mem_valid(pt_pfn, info) )
        {
            destroy_xen_mappings(rwva, rwva + (1UL << L2_PAGETABLE_SHIFT));

            l2_ro_mpt = l3e_to_l2e(l3_ro_mpt[l3_table_offset(va)]);
            l2e_write(&l2_ro_mpt[l2_table_offset(va)], l2e_empty());
        }
        i = ( i & ~((1UL << (L2_PAGETABLE_SHIFT - 3)) - 1)) +
              (1UL << (L2_PAGETABLE_SHIFT - 3));
    }

    destroy_compat_m2p_mapping(info);

    /* Brute-Force flush all TLB */
    flush_tlb_all();
    return;
}

/*
 * Allocate and map the compatibility mode machine-to-phys table.
 * spfn/epfn: the pfn ranges to be setup
 * free_s/free_e: the pfn ranges that is free still
 */
static int setup_compat_m2p_table(struct mem_hotadd_info *info)
{
    unsigned long i, va, smap, emap, rwva, epfn = info->epfn;
    unsigned int n;
    l3_pgentry_t *l3_ro_mpt = NULL;
    l2_pgentry_t *l2_ro_mpt = NULL;
    struct page_info *l1_pg;
    int err = 0;

    smap = info->spfn & (~((1UL << (L2_PAGETABLE_SHIFT - 2)) -1));

    /*
     * Notice: For hot-added memory, only range below m2p_compat_vstart
     * will be filled up (assuming memory is discontinous when booting).
     */
    if   ((smap > ((RDWR_COMPAT_MPT_VIRT_END - RDWR_COMPAT_MPT_VIRT_START) >> 2)) )
        return 0;

    if ( epfn > ((RDWR_COMPAT_MPT_VIRT_END - RDWR_COMPAT_MPT_VIRT_START) >> 2) )
        epfn = (RDWR_COMPAT_MPT_VIRT_END - RDWR_COMPAT_MPT_VIRT_START) >> 2;

    emap = ( (epfn + ((1UL << (L2_PAGETABLE_SHIFT - 2)) - 1 )) &
                ~((1UL << (L2_PAGETABLE_SHIFT - 2)) - 1) );

    va = HIRO_COMPAT_MPT_VIRT_START +
         smap * sizeof(*compat_machine_to_phys_mapping);
    l3_ro_mpt = l4e_to_l3e(idle_pg_table[l4_table_offset(va)]);

    ASSERT(l3e_get_flags(l3_ro_mpt[l3_table_offset(va)]) & _PAGE_PRESENT);

    l2_ro_mpt = l3e_to_l2e(l3_ro_mpt[l3_table_offset(va)]);

#define MFN(x) (((x) << L2_PAGETABLE_SHIFT) / sizeof(unsigned int))
#define CNT ((sizeof(*frame_table) & -sizeof(*frame_table)) / \
             sizeof(*compat_machine_to_phys_mapping))
    BUILD_BUG_ON((sizeof(*frame_table) & -sizeof(*frame_table)) % \
                 sizeof(*compat_machine_to_phys_mapping));

    for ( i = smap; i < emap; i += (1UL << (L2_PAGETABLE_SHIFT - 2)) )
    {
        va = HIRO_COMPAT_MPT_VIRT_START +
              i * sizeof(*compat_machine_to_phys_mapping);

        rwva = RDWR_COMPAT_MPT_VIRT_START +
                i * sizeof(*compat_machine_to_phys_mapping);

        if (l2e_get_flags(l2_ro_mpt[l2_table_offset(va)]) & _PAGE_PRESENT)
            continue;

        for ( n = 0; n < CNT; ++n)
            if ( mfn_valid(i + n * PDX_GROUP_COUNT) )
                break;
        if ( n == CNT )
            continue;

        l1_pg = mfn_to_page(alloc_hotadd_mfn(info));
        err = map_pages_to_xen(rwva, page_to_mfn(l1_pg),
                               1UL << PAGETABLE_ORDER,
                               PAGE_HYPERVISOR);
        if ( err )
            break;
        /* Fill with INVALID_M2P_ENTRY. */
        memset((void *)rwva, 0xFF, 1UL << L2_PAGETABLE_SHIFT);
        /* NB. Cannot be GLOBAL as the ptes get copied into per-VM space. */
        l2e_write(&l2_ro_mpt[l2_table_offset(va)], l2e_from_page(l1_pg, _PAGE_PSE|_PAGE_PRESENT));
    }
#undef CNT
#undef MFN
    return err;
}

/*
 * Allocate and map the machine-to-phys table.
 * The L3 for RO/RWRW MPT and the L2 for compatible MPT should be setup already
 */
static int setup_m2p_table(struct mem_hotadd_info *info)
{
    unsigned long i, va, smap, emap;
    unsigned int n, memflags;
    l2_pgentry_t *l2_ro_mpt = NULL;
    l3_pgentry_t *l3_ro_mpt = NULL;
    struct page_info *l1_pg, *l2_pg;
    int ret = 0;

    ASSERT(l4e_get_flags(idle_pg_table[l4_table_offset(RO_MPT_VIRT_START)])
            & _PAGE_PRESENT);
    l3_ro_mpt = l4e_to_l3e(idle_pg_table[l4_table_offset(RO_MPT_VIRT_START)]);

    smap = (info->spfn & (~((1UL << (L2_PAGETABLE_SHIFT - 3)) -1)));
    emap = ((info->epfn + ((1UL << (L2_PAGETABLE_SHIFT - 3)) - 1 )) &
                ~((1UL << (L2_PAGETABLE_SHIFT - 3)) -1));

    va = RO_MPT_VIRT_START + smap * sizeof(*machine_to_phys_mapping);

#define MFN(x) (((x) << L2_PAGETABLE_SHIFT) / sizeof(unsigned long))
#define CNT ((sizeof(*frame_table) & -sizeof(*frame_table)) / \
             sizeof(*machine_to_phys_mapping))

    BUILD_BUG_ON((sizeof(*frame_table) & -sizeof(*frame_table)) % \
                 sizeof(*machine_to_phys_mapping));

    i = smap;
    while ( i < emap )
    {
        switch ( m2p_mapped(i) )
        {
        case M2P_1G_MAPPED:
            i = ( i & ~((1UL << (L3_PAGETABLE_SHIFT - 3)) - 1)) +
                (1UL << (L3_PAGETABLE_SHIFT - 3));
            continue;
        case M2P_2M_MAPPED:
            i = (i & ~((1UL << (L2_PAGETABLE_SHIFT - 3)) - 1)) +
                (1UL << (L2_PAGETABLE_SHIFT - 3));
            continue;
        default:
            break;
        }

        va = RO_MPT_VIRT_START + i * sizeof(*machine_to_phys_mapping);
        memflags = MEMF_node(phys_to_nid(i << PAGE_SHIFT));

        for ( n = 0; n < CNT; ++n)
            if ( mfn_valid(i + n * PDX_GROUP_COUNT) )
                break;
        if ( n == CNT )
            l1_pg = NULL;
        else
        {
            l1_pg = mfn_to_page(alloc_hotadd_mfn(info));
            ret = map_pages_to_xen(
                        RDWR_MPT_VIRT_START + i * sizeof(unsigned long),
                        page_to_mfn(l1_pg),
                        1UL << PAGETABLE_ORDER,
                        PAGE_HYPERVISOR);
            if ( ret )
                goto error;
            /* Fill with INVALID_M2P_ENTRY. */
            memset((void *)(RDWR_MPT_VIRT_START + i * sizeof(unsigned long)),
                   0xFF, 1UL << L2_PAGETABLE_SHIFT);

            ASSERT(!(l3e_get_flags(l3_ro_mpt[l3_table_offset(va)]) &
                  _PAGE_PSE));
            if ( l3e_get_flags(l3_ro_mpt[l3_table_offset(va)]) &
              _PAGE_PRESENT )
                l2_ro_mpt = l3e_to_l2e(l3_ro_mpt[l3_table_offset(va)]) +
                  l2_table_offset(va);
            else
            {
                l2_pg = alloc_domheap_page(NULL, memflags);

                if (!l2_pg)
                {
                    ret = -ENOMEM;
                    goto error;
                }

                l2_ro_mpt = page_to_virt(l2_pg);
                clear_page(l2_ro_mpt);
                l3e_write(&l3_ro_mpt[l3_table_offset(va)],
                  l3e_from_page(l2_pg, __PAGE_HYPERVISOR | _PAGE_USER));
               l2_ro_mpt += l2_table_offset(va);
            }

            /* NB. Cannot be GLOBAL as shadow_mode_translate reuses this area. */
            l2e_write(l2_ro_mpt, l2e_from_page(l1_pg,
                   /*_PAGE_GLOBAL|*/_PAGE_PSE|_PAGE_USER|_PAGE_PRESENT));
        }
        if ( !((unsigned long)l2_ro_mpt & ~PAGE_MASK) )
            l2_ro_mpt = NULL;
        i += ( 1UL << (L2_PAGETABLE_SHIFT - 3));
    }
#undef CNT
#undef MFN

    ret = setup_compat_m2p_table(info);
error:
    return ret;
}

void __init paging_init(void)
{
    unsigned long i, mpt_size, va;
    unsigned int n, memflags;
    l3_pgentry_t *l3_ro_mpt;
    l2_pgentry_t *l2_ro_mpt = NULL;
    struct page_info *l1_pg, *l2_pg, *l3_pg;

    /*
     * We setup the L3s for 1:1 mapping if host support memory hotplug
     * to avoid sync the 1:1 mapping on page fault handler
     */
    if ( mem_hotplug )
    {
        unsigned long va;

        for ( va = DIRECTMAP_VIRT_START;
              va < DIRECTMAP_VIRT_END;
              va += (1UL << L4_PAGETABLE_SHIFT) )
        {
            if ( !(l4e_get_flags(idle_pg_table[l4_table_offset(va)]) &
                  _PAGE_PRESENT) )
            {
                l3_pg = alloc_domheap_page(NULL, 0);
                if ( !l3_pg )
                    goto nomem;
                l3_ro_mpt = page_to_virt(l3_pg);
                clear_page(l3_ro_mpt);
                l4e_write(&idle_pg_table[l4_table_offset(va)],
                  l4e_from_page(l3_pg, __PAGE_HYPERVISOR));
            }
        }
    }

    /* Create user-accessible L2 directory to map the MPT for guests. */
    if ( (l3_pg = alloc_domheap_page(NULL, 0)) == NULL )
        goto nomem;
    l3_ro_mpt = page_to_virt(l3_pg);
    clear_page(l3_ro_mpt);
    l4e_write(&idle_pg_table[l4_table_offset(RO_MPT_VIRT_START)],
              l4e_from_page(l3_pg, __PAGE_HYPERVISOR | _PAGE_USER));

    /*
     * Allocate and map the machine-to-phys table.
     * This also ensures L3 is present for fixmaps.
     */
    mpt_size  = (max_page * BYTES_PER_LONG) + (1UL << L2_PAGETABLE_SHIFT) - 1;
    mpt_size &= ~((1UL << L2_PAGETABLE_SHIFT) - 1UL);
#define MFN(x) (((x) << L2_PAGETABLE_SHIFT) / sizeof(unsigned long))
#define CNT ((sizeof(*frame_table) & -sizeof(*frame_table)) / \
             sizeof(*machine_to_phys_mapping))
    BUILD_BUG_ON((sizeof(*frame_table) & ~sizeof(*frame_table)) % \
                 sizeof(*machine_to_phys_mapping));
    for ( i = 0; i < (mpt_size >> L2_PAGETABLE_SHIFT); i++ )
    {
        BUILD_BUG_ON(RO_MPT_VIRT_START & ((1UL << L3_PAGETABLE_SHIFT) - 1));
        va = RO_MPT_VIRT_START + (i << L2_PAGETABLE_SHIFT);
        memflags = MEMF_node(phys_to_nid(i <<
            (L2_PAGETABLE_SHIFT - 3 + PAGE_SHIFT)));

        if ( cpu_has_page1gb &&
             !((unsigned long)l2_ro_mpt & ~PAGE_MASK) &&
             (mpt_size >> L3_PAGETABLE_SHIFT) > (i >> PAGETABLE_ORDER) )
        {
            unsigned int k, holes;

            for ( holes = k = 0; k < 1 << PAGETABLE_ORDER; ++k)
            {
                for ( n = 0; n < CNT; ++n)
                    if ( mfn_valid(MFN(i + k) + n * PDX_GROUP_COUNT) )
                        break;
                if ( n == CNT )
                    ++holes;
            }
            if ( k == holes )
            {
                i += (1UL << PAGETABLE_ORDER) - 1;
                continue;
            }
            if ( holes == 0 &&
                 (l1_pg = alloc_domheap_pages(NULL, 2 * PAGETABLE_ORDER,
                                              memflags)) != NULL )
            {
                map_pages_to_xen(
                    RDWR_MPT_VIRT_START + (i << L2_PAGETABLE_SHIFT),
                    page_to_mfn(l1_pg),
                    1UL << (2 * PAGETABLE_ORDER),
                    PAGE_HYPERVISOR);
                memset((void *)(RDWR_MPT_VIRT_START + (i << L2_PAGETABLE_SHIFT)),
                       0x77, 1UL << L3_PAGETABLE_SHIFT);

                ASSERT(!l2_table_offset(va));
                /* NB. Cannot be GLOBAL as shadow_mode_translate reuses this area. */
                l3e_write(&l3_ro_mpt[l3_table_offset(va)],
                    l3e_from_page(l1_pg,
                        /*_PAGE_GLOBAL|*/_PAGE_PSE|_PAGE_USER|_PAGE_PRESENT));
                i += (1UL << PAGETABLE_ORDER) - 1;
                continue;
            }
        }

        for ( n = 0; n < CNT; ++n)
            if ( mfn_valid(MFN(i) + n * PDX_GROUP_COUNT) )
                break;
        if ( n == CNT )
            l1_pg = NULL;
        else if ( (l1_pg = alloc_domheap_pages(NULL, PAGETABLE_ORDER,
                                               memflags)) == NULL )
            goto nomem;
        else
        {
            map_pages_to_xen(
                RDWR_MPT_VIRT_START + (i << L2_PAGETABLE_SHIFT),
                page_to_mfn(l1_pg),
                1UL << PAGETABLE_ORDER,
                PAGE_HYPERVISOR);
            /* Fill with INVALID_M2P_ENTRY. */
            memset((void *)(RDWR_MPT_VIRT_START + (i << L2_PAGETABLE_SHIFT)),
                   0xFF, 1UL << L2_PAGETABLE_SHIFT);
        }
        if ( !((unsigned long)l2_ro_mpt & ~PAGE_MASK) )
        {
            if ( (l2_pg = alloc_domheap_page(NULL, memflags)) == NULL )
                goto nomem;
            l2_ro_mpt = page_to_virt(l2_pg);
            clear_page(l2_ro_mpt);
            l3e_write(&l3_ro_mpt[l3_table_offset(va)],
                      l3e_from_page(l2_pg, __PAGE_HYPERVISOR | _PAGE_USER));
            ASSERT(!l2_table_offset(va));
        }
        /* NB. Cannot be GLOBAL as shadow_mode_translate reuses this area. */
        if ( l1_pg )
            l2e_write(l2_ro_mpt, l2e_from_page(
                l1_pg, /*_PAGE_GLOBAL|*/_PAGE_PSE|_PAGE_USER|_PAGE_PRESENT));
        l2_ro_mpt++;
    }
#undef CNT
#undef MFN

    /* Create user-accessible L2 directory to map the MPT for compat guests. */
    BUILD_BUG_ON(l4_table_offset(RDWR_MPT_VIRT_START) !=
                 l4_table_offset(HIRO_COMPAT_MPT_VIRT_START));
    l3_ro_mpt = l4e_to_l3e(idle_pg_table[l4_table_offset(
        HIRO_COMPAT_MPT_VIRT_START)]);
    if ( (l2_pg = alloc_domheap_page(NULL, 0)) == NULL )
        goto nomem;
    compat_idle_pg_table_l2 = l2_ro_mpt = page_to_virt(l2_pg);
    clear_page(l2_ro_mpt);
    l3e_write(&l3_ro_mpt[l3_table_offset(HIRO_COMPAT_MPT_VIRT_START)],
              l3e_from_page(l2_pg, __PAGE_HYPERVISOR));
    l2_ro_mpt += l2_table_offset(HIRO_COMPAT_MPT_VIRT_START);
    /* Allocate and map the compatibility mode machine-to-phys table. */
    mpt_size = (mpt_size >> 1) + (1UL << (L2_PAGETABLE_SHIFT - 1));
    if ( mpt_size > RDWR_COMPAT_MPT_VIRT_END - RDWR_COMPAT_MPT_VIRT_START )
        mpt_size = RDWR_COMPAT_MPT_VIRT_END - RDWR_COMPAT_MPT_VIRT_START;
    mpt_size &= ~((1UL << L2_PAGETABLE_SHIFT) - 1UL);
    if ( (m2p_compat_vstart + mpt_size) < MACH2PHYS_COMPAT_VIRT_END )
        m2p_compat_vstart = MACH2PHYS_COMPAT_VIRT_END - mpt_size;
#define MFN(x) (((x) << L2_PAGETABLE_SHIFT) / sizeof(unsigned int))
#define CNT ((sizeof(*frame_table) & -sizeof(*frame_table)) / \
             sizeof(*compat_machine_to_phys_mapping))
    BUILD_BUG_ON((sizeof(*frame_table) & ~sizeof(*frame_table)) % \
                 sizeof(*compat_machine_to_phys_mapping));
    for ( i = 0; i < (mpt_size >> L2_PAGETABLE_SHIFT); i++, l2_ro_mpt++ )
    {
        memflags = MEMF_node(phys_to_nid(i <<
            (L2_PAGETABLE_SHIFT - 2 + PAGE_SHIFT)));
        for ( n = 0; n < CNT; ++n)
            if ( mfn_valid(MFN(i) + n * PDX_GROUP_COUNT) )
                break;
        if ( n == CNT )
            continue;
        if ( (l1_pg = alloc_domheap_pages(NULL, PAGETABLE_ORDER,
                                               memflags)) == NULL )
            goto nomem;
        map_pages_to_xen(
            RDWR_COMPAT_MPT_VIRT_START + (i << L2_PAGETABLE_SHIFT),
            page_to_mfn(l1_pg),
            1UL << PAGETABLE_ORDER,
            PAGE_HYPERVISOR);
        memset((void *)(RDWR_COMPAT_MPT_VIRT_START +
                        (i << L2_PAGETABLE_SHIFT)),
               0x55,
               1UL << L2_PAGETABLE_SHIFT);
        /* NB. Cannot be GLOBAL as the ptes get copied into per-VM space. */
        l2e_write(l2_ro_mpt, l2e_from_page(l1_pg, _PAGE_PSE|_PAGE_PRESENT));
    }
#undef CNT
#undef MFN

    machine_to_phys_mapping_valid = 1;

    /* Set up linear page table mapping. */
    l4e_write(&idle_pg_table[l4_table_offset(LINEAR_PT_VIRT_START)],
              l4e_from_paddr(__pa(idle_pg_table), __PAGE_HYPERVISOR));
    return;

 nomem:
    panic("Not enough memory for m2p table\n");    
}

void __init setup_idle_pagetable(void)
{
    /* Install per-domain mappings for idle domain. */
    l4e_write(&idle_pg_table[l4_table_offset(PERDOMAIN_VIRT_START)],
              l4e_from_page(
                  virt_to_page(idle_vcpu[0]->domain->arch.mm_perdomain_l3),
                  __PAGE_HYPERVISOR));
}

void __init zap_low_mappings(void)
{
    BUG_ON(num_online_cpus() != 1);

    /* Remove aliased mapping of first 1:1 PML4 entry. */
    l4e_write(&idle_pg_table[0], l4e_empty());
    flush_local(FLUSH_TLB_GLOBAL);

    /* Replace with mapping of the boot trampoline only. */
    map_pages_to_xen(trampoline_phys, trampoline_phys >> PAGE_SHIFT,
                     PFN_UP(trampoline_end - trampoline_start),
                     __PAGE_HYPERVISOR);
}

void *compat_arg_xlat_virt_base(void)
{
    return current->arch.compat_arg_xlat;
}

int setup_compat_arg_xlat(struct vcpu *v)
{
    unsigned int order = get_order_from_bytes(COMPAT_ARG_XLAT_SIZE);
    struct page_info *pg;

    pg = alloc_domheap_pages(NULL, order, 0);
    if ( pg == NULL )
        return -ENOMEM;

    v->arch.compat_arg_xlat = page_to_virt(pg);
    return 0;
}

void free_compat_arg_xlat(struct vcpu *v)
{
    unsigned int order = get_order_from_bytes(COMPAT_ARG_XLAT_SIZE);
    if ( v->arch.compat_arg_xlat != NULL )
        free_domheap_pages(virt_to_page(v->arch.compat_arg_xlat), order);
    v->arch.compat_arg_xlat = NULL;
}

void cleanup_frame_table(struct mem_hotadd_info *info)
{
    unsigned long sva, eva;
    l3_pgentry_t l3e;
    l2_pgentry_t l2e;
    unsigned long spfn, epfn;

    spfn = info->spfn;
    epfn = info->epfn;

    sva = (unsigned long)pdx_to_page(pfn_to_pdx(spfn));
    eva = (unsigned long)pdx_to_page(pfn_to_pdx(epfn));

    /* Intialize all page */
    memset(mfn_to_page(spfn), -1,
           (unsigned long)mfn_to_page(epfn) - (unsigned long)mfn_to_page(spfn));

    while (sva < eva)
    {
        l3e = l4e_to_l3e(idle_pg_table[l4_table_offset(sva)])[
          l3_table_offset(sva)];
        if ( !(l3e_get_flags(l3e) & _PAGE_PRESENT) ||
             (l3e_get_flags(l3e) & _PAGE_PSE) )
        {
            sva = (sva & ~((1UL << L3_PAGETABLE_SHIFT) - 1)) +
                    (1UL << L3_PAGETABLE_SHIFT);
            continue;
        }

        l2e = l3e_to_l2e(l3e)[l2_table_offset(sva)];
        ASSERT(l2e_get_flags(l2e) & _PAGE_PRESENT);

        if ( (l2e_get_flags(l2e) & (_PAGE_PRESENT | _PAGE_PSE)) ==
              (_PAGE_PSE | _PAGE_PRESENT) )
        {
            if (hotadd_mem_valid(l2e_get_pfn(l2e), info))
                destroy_xen_mappings(sva & ~((1UL << L2_PAGETABLE_SHIFT) - 1),
                         ((sva & ~((1UL << L2_PAGETABLE_SHIFT) -1 )) +
                            (1UL << L2_PAGETABLE_SHIFT) - 1));

            sva = (sva & ~((1UL << L2_PAGETABLE_SHIFT) -1 )) +
                  (1UL << L2_PAGETABLE_SHIFT);
            continue;
        }

        ASSERT(l1e_get_flags(l2e_to_l1e(l2e)[l1_table_offset(sva)]) &
                _PAGE_PRESENT);
         sva = (sva & ~((1UL << PAGE_SHIFT) - 1)) +
                    (1UL << PAGE_SHIFT);
    }

    /* Brute-Force flush all TLB */
    flush_tlb_all();
}

static int setup_frametable_chunk(void *start, void *end,
                                  struct mem_hotadd_info *info)
{
    unsigned long s = (unsigned long)start;
    unsigned long e = (unsigned long)end;
    unsigned long mfn;
    int err;

    ASSERT(!(s & ((1 << L2_PAGETABLE_SHIFT) - 1)));
    ASSERT(!(e & ((1 << L2_PAGETABLE_SHIFT) - 1)));

    for ( ; s < e; s += (1UL << L2_PAGETABLE_SHIFT))
    {
        mfn = alloc_hotadd_mfn(info);
        err = map_pages_to_xen(s, mfn, 1UL << PAGETABLE_ORDER,
                               PAGE_HYPERVISOR);
        if ( err )
            return err;
    }
    memset(start, -1, s - (unsigned long)start);

    return 0;
}

static int extend_frame_table(struct mem_hotadd_info *info)
{
    unsigned long cidx, nidx, eidx, spfn, epfn;

    spfn = info->spfn;
    epfn = info->epfn;

    eidx = (pfn_to_pdx(epfn) + PDX_GROUP_COUNT - 1) / PDX_GROUP_COUNT;
    nidx = cidx = pfn_to_pdx(spfn)/PDX_GROUP_COUNT;

    ASSERT( pfn_to_pdx(epfn) <= (DIRECTMAP_SIZE >> PAGE_SHIFT) &&
         (pfn_to_pdx(epfn) <= FRAMETABLE_SIZE / sizeof(struct page_info)) );

    if ( test_bit(cidx, pdx_group_valid) )
        cidx = find_next_zero_bit(pdx_group_valid, eidx, cidx);

    if ( cidx >= eidx )
        return 0;

    while ( cidx < eidx )
    {
        int err;

        nidx = find_next_bit(pdx_group_valid, eidx, cidx);
        if ( nidx >= eidx )
            nidx = eidx;
        err = setup_frametable_chunk(pdx_to_page(cidx * PDX_GROUP_COUNT ),
                                     pdx_to_page(nidx * PDX_GROUP_COUNT),
                                     info);
        if ( err )
            return err;

        cidx = find_next_zero_bit(pdx_group_valid, eidx, nidx);
    }

    memset(mfn_to_page(spfn), 0,
           (unsigned long)mfn_to_page(epfn) - (unsigned long)mfn_to_page(spfn));
    return 0;
}

void __init subarch_init_memory(void)
{
    unsigned long i, n, v, m2p_start_mfn;
    l3_pgentry_t l3e;
    l2_pgentry_t l2e;

    BUILD_BUG_ON(RDWR_MPT_VIRT_START & ((1UL << L3_PAGETABLE_SHIFT) - 1));
    BUILD_BUG_ON(RDWR_MPT_VIRT_END   & ((1UL << L3_PAGETABLE_SHIFT) - 1));
    /* M2P table is mappable read-only by privileged domains. */
    for ( v  = RDWR_MPT_VIRT_START;
          v != RDWR_MPT_VIRT_END;
          v += n << PAGE_SHIFT )
    {
        n = L2_PAGETABLE_ENTRIES * L1_PAGETABLE_ENTRIES;
        l3e = l4e_to_l3e(idle_pg_table[l4_table_offset(v)])[
            l3_table_offset(v)];
        if ( !(l3e_get_flags(l3e) & _PAGE_PRESENT) )
            continue;
        if ( !(l3e_get_flags(l3e) & _PAGE_PSE) )
        {
            n = L1_PAGETABLE_ENTRIES;
            l2e = l3e_to_l2e(l3e)[l2_table_offset(v)];
            if ( !(l2e_get_flags(l2e) & _PAGE_PRESENT) )
                continue;
            m2p_start_mfn = l2e_get_pfn(l2e);
        }
        else
        {
            m2p_start_mfn = l3e_get_pfn(l3e);
        }

        for ( i = 0; i < n; i++ )
        {
            struct page_info *page = mfn_to_page(m2p_start_mfn + i);
            share_xen_page_with_privileged_guests(page, XENSHARE_readonly);
        }
    }

    for ( v  = RDWR_COMPAT_MPT_VIRT_START;
          v != RDWR_COMPAT_MPT_VIRT_END;
          v += 1 << L2_PAGETABLE_SHIFT )
    {
        l3e = l4e_to_l3e(idle_pg_table[l4_table_offset(v)])[
            l3_table_offset(v)];
        if ( !(l3e_get_flags(l3e) & _PAGE_PRESENT) )
            continue;
        l2e = l3e_to_l2e(l3e)[l2_table_offset(v)];
        if ( !(l2e_get_flags(l2e) & _PAGE_PRESENT) )
            continue;
        m2p_start_mfn = l2e_get_pfn(l2e);

        for ( i = 0; i < L1_PAGETABLE_ENTRIES; i++ )
        {
            struct page_info *page = mfn_to_page(m2p_start_mfn + i);
            share_xen_page_with_privileged_guests(page, XENSHARE_readonly);
        }
    }
}

long subarch_memory_op(int op, XEN_GUEST_HANDLE(void) arg)
{
    struct xen_machphys_mfn_list xmml;
    l3_pgentry_t l3e;
    l2_pgentry_t l2e;
    unsigned long v;
    xen_pfn_t mfn, last_mfn;
    unsigned int i;
    long rc = 0;

    switch ( op )
    {
    case XENMEM_machphys_mfn_list:
        if ( copy_from_guest(&xmml, arg, 1) )
            return -EFAULT;

        BUILD_BUG_ON(RDWR_MPT_VIRT_START & ((1UL << L3_PAGETABLE_SHIFT) - 1));
        BUILD_BUG_ON(RDWR_MPT_VIRT_END   & ((1UL << L3_PAGETABLE_SHIFT) - 1));
        for ( i = 0, v = RDWR_MPT_VIRT_START, last_mfn = 0;
              (i != xmml.max_extents) &&
              (v < (unsigned long)(machine_to_phys_mapping + max_page));
              i++, v += 1UL << L2_PAGETABLE_SHIFT )
        {
            l3e = l4e_to_l3e(idle_pg_table[l4_table_offset(v)])[
                l3_table_offset(v)];
            if ( !(l3e_get_flags(l3e) & _PAGE_PRESENT) )
                mfn = last_mfn;
            else if ( !(l3e_get_flags(l3e) & _PAGE_PSE) )
            {
                l2e = l3e_to_l2e(l3e)[l2_table_offset(v)];
                if ( l2e_get_flags(l2e) & _PAGE_PRESENT )
                    mfn = l2e_get_pfn(l2e);
                else
                    mfn = last_mfn;
            }
            else
            {
                mfn = l3e_get_pfn(l3e)
                    + (l2_table_offset(v) << PAGETABLE_ORDER);
            }
            ASSERT(mfn);
            if ( copy_to_guest_offset(xmml.extent_start, i, &mfn, 1) )
                return -EFAULT;
            last_mfn = mfn;
        }

        xmml.nr_extents = i;
        if ( copy_to_guest(arg, &xmml, 1) )
            return -EFAULT;

        break;

    case XENMEM_get_sharing_freed_pages:
        return mem_sharing_get_nr_saved_mfns();

    case XENMEM_get_sharing_shared_pages:
        return mem_sharing_get_nr_shared_mfns();

    case XENMEM_paging_op:
    case XENMEM_access_op:
    {
        xen_mem_event_op_t meo;
        if ( copy_from_guest(&meo, arg, 1) )
            return -EFAULT;
        rc = do_mem_event_op(op, meo.domain, (void *) &meo);
        if ( !rc && copy_to_guest(arg, &meo, 1) )
            return -EFAULT;
        break;
    }
    case XENMEM_sharing_op:
    {
        xen_mem_sharing_op_t mso;
        if ( copy_from_guest(&mso, arg, 1) )
            return -EFAULT;
        if ( mso.op == XENMEM_sharing_op_audit )
            return mem_sharing_audit(); 
        rc = do_mem_event_op(op, mso.domain, (void *) &mso);
        if ( !rc && copy_to_guest(arg, &mso, 1) )
            return -EFAULT;
        break;
    }

    default:
        rc = -ENOSYS;
        break;
    }

    return rc;
}

long do_stack_switch(unsigned long ss, unsigned long esp)
{
    fixup_guest_stack_selector(current->domain, ss);
    current->arch.pv_vcpu.kernel_ss = ss;
    current->arch.pv_vcpu.kernel_sp = esp;
    return 0;
}

long do_set_segment_base(unsigned int which, unsigned long base)
{
    struct vcpu *v = current;
    long ret = 0;

    switch ( which )
    {
    case SEGBASE_FS:
        if ( wrmsr_safe(MSR_FS_BASE, base) )
            ret = -EFAULT;
        else
            v->arch.pv_vcpu.fs_base = base;
        break;

    case SEGBASE_GS_USER:
        if ( wrmsr_safe(MSR_SHADOW_GS_BASE, base) )
            ret = -EFAULT;
        else
            v->arch.pv_vcpu.gs_base_user = base;
        break;

    case SEGBASE_GS_KERNEL:
        if ( wrmsr_safe(MSR_GS_BASE, base) )
            ret = -EFAULT;
        else
            v->arch.pv_vcpu.gs_base_kernel = base;
        break;

    case SEGBASE_GS_USER_SEL:
        __asm__ __volatile__ (
            "     swapgs              \n"
            "1:   movl %k0,%%gs       \n"
            "    "safe_swapgs"        \n"
            ".section .fixup,\"ax\"   \n"
            "2:   xorl %k0,%k0        \n"
            "     jmp  1b             \n"
            ".previous                \n"
            _ASM_EXTABLE(1b, 2b)
            : : "r" (base&0xffff) );
        break;

    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}


/* Returns TRUE if given descriptor is valid for GDT or LDT. */
int check_descriptor(const struct domain *dom, struct desc_struct *d)
{
    u32 a = d->a, b = d->b;
    u16 cs;
    unsigned int dpl;

    /* A not-present descriptor will always fault, so is safe. */
    if ( !(b & _SEGMENT_P) ) 
        goto good;

    /* Check and fix up the DPL. */
    dpl = (b >> 13) & 3;
    __fixup_guest_selector(dom, dpl);
    b = (b & ~_SEGMENT_DPL) | (dpl << 13);

    /* All code and data segments are okay. No base/limit checking. */
    if ( (b & _SEGMENT_S) )
    {
        if ( is_pv_32bit_domain(dom) )
        {
            unsigned long base, limit;

            if ( b & _SEGMENT_L )
                goto bad;

            /*
             * Older PAE Linux guests use segments which are limited to
             * 0xf6800000. Extend these to allow access to the larger read-only
             * M2P table available in 32on64 mode.
             */
            base = (b & (0xff << 24)) | ((b & 0xff) << 16) | (a >> 16);

            limit = (b & 0xf0000) | (a & 0xffff);
            limit++; /* We add one because limit is inclusive. */

            if ( (b & _SEGMENT_G) )
                limit <<= 12;

            if ( (base == 0) && (limit > HYPERVISOR_COMPAT_VIRT_START(dom)) )
            {
                a |= 0x0000ffff;
                b |= 0x000f0000;
            }
        }

        goto good;
    }

    /* Invalid type 0 is harmless. It is used for 2nd half of a call gate. */
    if ( (b & _SEGMENT_TYPE) == 0x000 )
        goto good;

    /* Everything but a call gate is discarded here. */
    if ( (b & _SEGMENT_TYPE) != 0xc00 )
        goto bad;

    /* Validate the target code selector. */
    cs = a >> 16;
    if ( !guest_gate_selector_okay(dom, cs) )
        goto bad;
    /*
     * Force DPL to zero, causing a GP fault with its error code indicating
     * the gate in use, allowing emulation. This is necessary because with
     * native guests (kernel in ring 3) call gates cannot be used directly
     * to transition from user to kernel mode (and whether a gate is used
     * to enter the kernel can only be determined when the gate is being
     * used), and with compat guests call gates cannot be used at all as
     * there are only 64-bit ones.
     * Store the original DPL in the selector's RPL field.
     */
    b &= ~_SEGMENT_DPL;
    cs = (cs & ~3) | dpl;
    a = (a & 0xffffU) | (cs << 16);

    /* Reserved bits must be zero. */
    if ( b & (is_pv_32bit_domain(dom) ? 0xe0 : 0xff) )
        goto bad;
        
 good:
    d->a = a;
    d->b = b;
    return 1;
 bad:
    return 0;
}

int pagefault_by_memadd(unsigned long addr, struct cpu_user_regs *regs)
{
    struct domain *d = current->domain;

    return mem_hotplug && guest_mode(regs) && is_pv_32bit_domain(d) &&
           (addr >= HYPERVISOR_COMPAT_VIRT_START(d)) &&
           (addr < MACH2PHYS_COMPAT_VIRT_END);
}

int handle_memadd_fault(unsigned long addr, struct cpu_user_regs *regs)
{
    struct domain *d = current->domain;
    l4_pgentry_t *pl4e = NULL;
    l4_pgentry_t l4e;
    l3_pgentry_t  *pl3e = NULL;
    l3_pgentry_t l3e;
    l2_pgentry_t *pl2e = NULL;
    l2_pgentry_t l2e, idle_l2e;
    unsigned long mfn, idle_index;
    int ret = 0;

    if (!is_pv_32on64_domain(d))
        return 0;

    if ( (addr < HYPERVISOR_COMPAT_VIRT_START(d)) ||
         (addr >= MACH2PHYS_COMPAT_VIRT_END) )
        return 0;

    mfn = (read_cr3()) >> PAGE_SHIFT;

    pl4e = map_domain_page(mfn);

    l4e = pl4e[0];

    if (!(l4e_get_flags(l4e) & _PAGE_PRESENT))
        goto unmap;

    mfn = l4e_get_pfn(l4e);
    /* We don't need get page type here since it is current CR3 */
    pl3e = map_domain_page(mfn);

    l3e = pl3e[3];

    if ( !(l3e_get_flags(l3e) & _PAGE_PRESENT) )
        goto unmap;

    mfn = l3e_get_pfn(l3e);
    pl2e = map_domain_page(mfn);

    l2e = pl2e[l2_table_offset(addr)];

    if ( !(l2e_get_flags(l2e) & _PAGE_PRESENT))
        goto unmap;

    idle_index = (l2_table_offset(addr) -
                        COMPAT_L2_PAGETABLE_FIRST_XEN_SLOT(d))/
                  sizeof(l2_pgentry_t);
    idle_l2e = compat_idle_pg_table_l2[idle_index];
    if (!(l2e_get_flags(idle_l2e) & _PAGE_PRESENT))
        goto unmap;

    memcpy(&pl2e[l2_table_offset(addr)],
            &compat_idle_pg_table_l2[idle_index],
            sizeof(l2_pgentry_t));

    ret = EXCRET_fault_fixed;

unmap:
    if ( pl4e )
        unmap_domain_page(pl4e);
    if ( pl3e )
        unmap_domain_page(pl3e);
    if ( pl2e )
        unmap_domain_page(pl2e);

    return ret;
}

void domain_set_alloc_bitsize(struct domain *d)
{
    if ( !is_pv_32on64_domain(d) ||
         (MACH2PHYS_COMPAT_NR_ENTRIES(d) >= max_page) ||
         d->arch.physaddr_bitsize > 0 )
        return;
    d->arch.physaddr_bitsize =
        /* 2^n entries can be contained in guest's p2m mapping space */
        fls(MACH2PHYS_COMPAT_NR_ENTRIES(d)) - 1
        /* 2^n pages -> 2^(n+PAGE_SHIFT) bits */
        + PAGE_SHIFT;
}

unsigned int domain_clamp_alloc_bitsize(struct domain *d, unsigned int bits)
{
    if ( (d == NULL) || (d->arch.physaddr_bitsize == 0) )
        return bits;
    return min(d->arch.physaddr_bitsize, bits);
}

int transfer_pages_to_heap(struct mem_hotadd_info *info)
{
    unsigned long i;
    struct page_info *pg;

    /*
     * Mark the allocated page before put free pages to buddy allocator
     * to avoid merge in free_heap_pages
     */
    for (i = info->spfn; i < info->cur; i++)
    {
        pg = mfn_to_page(i);
        pg->count_info = PGC_state_inuse;
    }

    init_domheap_pages(pfn_to_paddr(info->cur), pfn_to_paddr(info->epfn));

    return 0;
}

int mem_hotadd_check(unsigned long spfn, unsigned long epfn)
{
    unsigned long s, e, length, sidx, eidx;

    if ( (spfn >= epfn) )
        return 0;

    if (pfn_to_pdx(epfn) > (FRAMETABLE_SIZE / sizeof(*frame_table)))
        return 0;

    if ( (spfn | epfn) & ((1UL << PAGETABLE_ORDER) - 1) )
        return 0;

    if ( (spfn | epfn) & pfn_hole_mask )
        return 0;

    /* Make sure the new range is not present now */
    sidx = ((pfn_to_pdx(spfn) + PDX_GROUP_COUNT - 1)  & ~(PDX_GROUP_COUNT - 1))
            / PDX_GROUP_COUNT;
    eidx = (pfn_to_pdx(epfn - 1) & ~(PDX_GROUP_COUNT - 1)) / PDX_GROUP_COUNT;
    if (sidx >= eidx)
        return 0;

    s = find_next_zero_bit(pdx_group_valid, eidx, sidx);
    if ( s > eidx )
        return 0;
    e = find_next_bit(pdx_group_valid, eidx, s);
    if ( e < eidx )
        return 0;

    /* Caculate at most required m2p/compat m2p/frametable pages */
    s = (spfn & ~((1UL << (L2_PAGETABLE_SHIFT - 3)) - 1));
    e = (epfn + (1UL << (L2_PAGETABLE_SHIFT - 3)) - 1) &
            ~((1UL << (L2_PAGETABLE_SHIFT - 3)) - 1);

    length = (e - s) * sizeof(unsigned long);

    s = (spfn & ~((1UL << (L2_PAGETABLE_SHIFT - 2)) - 1));
    e = (epfn + (1UL << (L2_PAGETABLE_SHIFT - 2)) - 1) &
            ~((1UL << (L2_PAGETABLE_SHIFT - 2)) - 1);

    e = min_t(unsigned long, e,
            (RDWR_COMPAT_MPT_VIRT_END - RDWR_COMPAT_MPT_VIRT_START) >> 2);

    if ( e > s )
        length += (e -s) * sizeof(unsigned int);

    s = pfn_to_pdx(spfn) & ~(PDX_GROUP_COUNT - 1);
    e = ( pfn_to_pdx(epfn) + (PDX_GROUP_COUNT - 1) ) & ~(PDX_GROUP_COUNT - 1);

    length += (e - s) * sizeof(struct page_info);

    if ((length >> PAGE_SHIFT) > (epfn - spfn))
        return 0;

    return 1;
}

/*
 * A bit paranoid for memory allocation failure issue since
 * it may be reason for memory add
 */
int memory_add(unsigned long spfn, unsigned long epfn, unsigned int pxm)
{
    struct mem_hotadd_info info;
    int ret, node;
    unsigned long old_max = max_page, old_total = total_pages;
    unsigned long old_node_start, old_node_span, orig_online;
    unsigned long i;

    dprintk(XENLOG_INFO, "memory_add %lx ~ %lx with pxm %x\n", spfn, epfn, pxm);

    if ( !mem_hotadd_check(spfn, epfn) )
        return -EINVAL;

    if ( (node = setup_node(pxm)) == -1 )
        return -EINVAL;

    if ( !valid_numa_range(spfn << PAGE_SHIFT, epfn << PAGE_SHIFT, node) )
    {
        dprintk(XENLOG_WARNING, "spfn %lx ~ epfn %lx pxm %x node %x"
            "is not numa valid", spfn, epfn, pxm, node);
        return -EINVAL;
    }

    ret =  map_pages_to_xen((unsigned long)mfn_to_virt(spfn), spfn,
                            epfn - spfn, PAGE_HYPERVISOR);
     if ( ret )
        return ret;

    old_node_start = NODE_DATA(node)->node_start_pfn;
    old_node_span = NODE_DATA(node)->node_spanned_pages;
    orig_online = node_online(node);

    if ( !orig_online )
    {
        dprintk(XENLOG_WARNING, "node %x pxm %x is not online\n",node, pxm);
        NODE_DATA(node)->node_id = node;
        NODE_DATA(node)->node_start_pfn = spfn;
        NODE_DATA(node)->node_spanned_pages =
                epfn - node_start_pfn(node);
        node_set_online(node);
    }else
    {
        if (NODE_DATA(node)->node_start_pfn > spfn)
            NODE_DATA(node)->node_start_pfn = spfn;
        if (node_end_pfn(node) < epfn)
            NODE_DATA(node)->node_spanned_pages = epfn - node_start_pfn(node);
    }

    ret = -EINVAL;
    info.spfn = spfn;
    info.epfn = epfn;
    info.cur = spfn;

    ret = extend_frame_table(&info);
    if (ret)
        goto destroy_frametable;

    /* Set max_page as setup_m2p_table will use it*/
    if (max_page < epfn)
    {
        max_page = epfn;
        max_pdx = pfn_to_pdx(max_page - 1) + 1;
    }
    total_pages += epfn - spfn;

    set_pdx_range(spfn, epfn);
    ret = setup_m2p_table(&info);

    if ( ret )
        goto destroy_m2p;

    if ( !need_iommu(dom0) )
    {
        for ( i = spfn; i < epfn; i++ )
            if ( iommu_map_page(dom0, i, i, IOMMUF_readable|IOMMUF_writable) )
                break;
        if ( i != epfn )
        {
            while (i-- > old_max)
                iommu_unmap_page(dom0, i);
            goto destroy_m2p;
        }
    }

    /* We can't revert any more */
    transfer_pages_to_heap(&info);

    share_hotadd_m2p_table(&info);

    return 0;

destroy_m2p:
    destroy_m2p_mapping(&info);
    max_page = old_max;
    total_pages = old_total;
    max_pdx = pfn_to_pdx(max_page - 1) + 1;
destroy_frametable:
    cleanup_frame_table(&info);
    destroy_xen_mappings((unsigned long)mfn_to_virt(spfn),
                         (unsigned long)mfn_to_virt(epfn));

    if ( !orig_online )
        node_set_offline(node);
    NODE_DATA(node)->node_start_pfn = old_node_start;
    NODE_DATA(node)->node_spanned_pages = old_node_span;

    destroy_xen_mappings((unsigned long)mfn_to_virt(spfn),
                         (unsigned long)mfn_to_virt(epfn));
    return ret;
}

#include "compat/mm.c"

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
