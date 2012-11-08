/*
 * QEMU Xen FV Machine
 *
 * Copyright (c) 2003-2007 Fabrice Bellard
 * Copyright (c) 2007 Red Hat
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

#include "hw.h"
#include "pc.h"
#include "boards.h"
#include "exec-all.h"
#include "qemu-xen.h"
#include "qemu-aio.h"
#include "xen_backend.h"

#include <xen/hvm/params.h>
#include <sys/mman.h>

#if defined(MAPCACHE)

#define BITS_PER_LONG (sizeof(long)*8)
#define BITS_TO_LONGS(bits) \
    (((bits)+BITS_PER_LONG-1)/BITS_PER_LONG)
#define DECLARE_BITMAP(name,bits) \
    unsigned long name[BITS_TO_LONGS(bits)]
#define test_bit(bit,map) \
    (!!((map)[(bit)/BITS_PER_LONG] & (1UL << ((bit)%BITS_PER_LONG))))

struct map_cache {
    unsigned long paddr_index;
    uint8_t      *vaddr_base;
    DECLARE_BITMAP(valid_mapping, MCACHE_BUCKET_SIZE>>XC_PAGE_SHIFT);
    uint8_t lock;
    struct map_cache *next;
};

struct map_cache_rev {
    uint8_t      *vaddr_req;
    unsigned long paddr_index;
    TAILQ_ENTRY(map_cache_rev) next;
};

static struct map_cache *mapcache_entry;
static unsigned long nr_buckets;
TAILQ_HEAD(map_cache_head, map_cache_rev) locked_entries = TAILQ_HEAD_INITIALIZER(locked_entries);

/* For most cases (>99.9%), the page address is the same. */
static unsigned long last_address_page = ~0UL;
static uint8_t      *last_address_vaddr;

static int qemu_map_cache_init(void)
{
    unsigned long size;

    nr_buckets = (((MAX_MCACHE_SIZE >> XC_PAGE_SHIFT) +
                   (1UL << (MCACHE_BUCKET_SHIFT - XC_PAGE_SHIFT)) - 1) >>
                  (MCACHE_BUCKET_SHIFT - XC_PAGE_SHIFT));

    /*
     * Use mmap() directly: lets us allocate a big hash table with no up-front
     * cost in storage space. The OS will allocate memory only for the buckets
     * that we actually use. All others will contain all zeroes.
     */
    size = nr_buckets * sizeof(struct map_cache);
    size = (size + XC_PAGE_SIZE - 1) & ~(XC_PAGE_SIZE - 1);
    fprintf(logfile, "qemu_map_cache_init nr_buckets = %lx size %lu\n", nr_buckets, size);
    mapcache_entry = mmap(NULL, size, PROT_READ|PROT_WRITE,
                          MAP_SHARED|MAP_ANON, -1, 0);
    if (mapcache_entry == MAP_FAILED) {
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

static void qemu_remap_bucket(struct map_cache *entry,
                              unsigned long address_index)
{
    uint8_t *vaddr_base;
    xen_pfn_t pfns[MCACHE_BUCKET_SIZE >> XC_PAGE_SHIFT];
    int err[MCACHE_BUCKET_SIZE >> XC_PAGE_SHIFT];
    unsigned int i, j;

    if (entry->vaddr_base != NULL) {
        errno = munmap(entry->vaddr_base, MCACHE_BUCKET_SIZE);
        if (errno) {
            fprintf(logfile, "unmap fails %d\n", errno);
            exit(-1);
        }
    }

    for (i = 0; i < MCACHE_BUCKET_SIZE >> XC_PAGE_SHIFT; i++)
        pfns[i] = (address_index << (MCACHE_BUCKET_SHIFT-XC_PAGE_SHIFT)) + i;

    vaddr_base = xc_map_foreign_bulk(xc_handle, domid, PROT_READ|PROT_WRITE,
                                     pfns, err,
                                     MCACHE_BUCKET_SIZE >> XC_PAGE_SHIFT);
    if (vaddr_base == NULL) {
        fprintf(logfile, "xc_map_foreign_bulk error %d\n", errno);
        exit(-1);
    }

    entry->vaddr_base  = vaddr_base;
    entry->paddr_index = address_index;

    for (i = 0; i < MCACHE_BUCKET_SIZE >> XC_PAGE_SHIFT; i += BITS_PER_LONG) {
        unsigned long word = 0;
        j = ((i + BITS_PER_LONG) > (MCACHE_BUCKET_SIZE >> XC_PAGE_SHIFT)) ?
            (MCACHE_BUCKET_SIZE >> XC_PAGE_SHIFT) % BITS_PER_LONG : BITS_PER_LONG;
        while (j > 0)
            word = (word << 1) | !err[i + --j];
        entry->valid_mapping[i / BITS_PER_LONG] = word;
    }
}

uint8_t *qemu_map_cache(target_phys_addr_t phys_addr, uint8_t lock)
{
    struct map_cache *entry, *pentry = NULL;
    unsigned long address_index  = phys_addr >> MCACHE_BUCKET_SHIFT;
    unsigned long address_offset = phys_addr & (MCACHE_BUCKET_SIZE-1);

    if ((phys_addr >> XC_PAGE_SHIFT) == last_address_page && !lock)
        return last_address_vaddr + address_offset;

    entry = &mapcache_entry[address_index % nr_buckets];

    while (entry && entry->lock && entry->paddr_index != address_index && entry->vaddr_base) {
        pentry = entry;
        entry = entry->next;
    }
    if (!entry) {
        entry = qemu_mallocz(sizeof(struct map_cache));
        pentry->next = entry;
        qemu_remap_bucket(entry, address_index);
    } else if (!entry->lock) {
        if (!entry->vaddr_base || entry->paddr_index != address_index || !test_bit(address_offset>>XC_PAGE_SHIFT, entry->valid_mapping))
            qemu_remap_bucket(entry, address_index);
    }

    if (!test_bit(address_offset>>XC_PAGE_SHIFT, entry->valid_mapping)) {
        last_address_page = ~0UL;
        return NULL;
    }

    last_address_page = phys_addr >> XC_PAGE_SHIFT;
    last_address_vaddr = entry->vaddr_base;
    if (lock) {
        struct map_cache_rev *reventry = qemu_mallocz(sizeof(struct map_cache_rev));
        entry->lock++;
        reventry->vaddr_req = last_address_vaddr + address_offset;
        reventry->paddr_index = address_index;
        TAILQ_INSERT_TAIL(&locked_entries, reventry, next);
    }

    return last_address_vaddr + address_offset;
}

void qemu_invalidate_entry(uint8_t *buffer)
{
    struct map_cache *entry = NULL, *pentry = NULL;
    struct map_cache_rev *reventry;
    unsigned long paddr_index;
    int found = 0;
    
    TAILQ_FOREACH(reventry, &locked_entries, next) {
        if (reventry->vaddr_req == buffer) {
            paddr_index = reventry->paddr_index;
            found = 1;
            break;
        }
    }
    if (!found) {
        fprintf(stderr, "qemu_invalidate_entry: could not find %p\n", buffer);
        TAILQ_FOREACH(reventry, &locked_entries, next) {
            fprintf(stderr, "   %lx -> %p is present\n", reventry->paddr_index, reventry->vaddr_req);
        }
        return;
    }
    TAILQ_REMOVE(&locked_entries, reventry, next);
    qemu_free(reventry);

    if (last_address_page >> (MCACHE_BUCKET_SHIFT - XC_PAGE_SHIFT) == paddr_index) {
        last_address_page =  ~0UL;
    }

    entry = &mapcache_entry[paddr_index % nr_buckets];
    while (entry && entry->paddr_index != paddr_index) {
        pentry = entry;
        entry = entry->next;
    }
    if (!entry) {
        fprintf(logfile, "Trying to unmap address %p that is not in the mapcache!\n", buffer);
        return;
    }
    entry->lock--;
    if (entry->lock > 0 || pentry == NULL)
        return;

    pentry->next = entry->next;
    errno = munmap(entry->vaddr_base, MCACHE_BUCKET_SIZE);
    if (errno) {
        fprintf(logfile, "unmap fails %d\n", errno);
        exit(-1);
    }
    qemu_free(entry);
}

void qemu_invalidate_map_cache(void)
{
    unsigned long i;
    struct map_cache_rev *reventry;

    qemu_aio_flush();

    TAILQ_FOREACH(reventry, &locked_entries, next) {
        fprintf(stderr, "There should be no locked mappings at this time, but %lx -> %p is present\n", reventry->paddr_index, reventry->vaddr_req);
    }

    mapcache_lock();

    for (i = 0; i < nr_buckets; i++) {
        struct map_cache *entry = &mapcache_entry[i];

        if (entry->vaddr_base == NULL)
            continue;

        errno = munmap(entry->vaddr_base, MCACHE_BUCKET_SIZE);
        if (errno) {
            fprintf(logfile, "unmap fails %d\n", errno);
            exit(-1);
        }

        entry->paddr_index = 0;
        entry->vaddr_base  = NULL;
    }

    last_address_page =  ~0UL;
    last_address_vaddr = NULL;

    mapcache_unlock();
}
#else
uint8_t *qemu_map_cache(target_phys_addr_t phys_addr, uint8_t lock)
{
	return phys_ram_addr(phys_addr);
}

void qemu_invalidate_map_cache(void) {};

void qemu_invalidate_entry(uint8_t *buffer) {};

#endif /* defined(MAPCACHE) */


static void xen_init_fv(ram_addr_t ram_size, int vga_ram_size,
			const char *boot_device,
			const char *kernel_filename,const char *kernel_cmdline,
                        const char *initrd_filename, const char *cpu_model,
                        const char *direct_pci)
{
    unsigned long ioreq_pfn;
    extern void *shared_page;
    extern void *buffered_io_page;
#ifdef __ia64__
    unsigned long nr_pages;
    xen_pfn_t *page_array;
    extern void *buffered_pio_page;
    int i;
#endif

#if defined(__i386__) || defined(__x86_64__)

    if (qemu_map_cache_init()) {
        fprintf(logfile, "qemu_map_cache_init returned: error %d\n", errno);
        exit(-1);
    }
#endif

#ifdef CONFIG_STUBDOM /* the hvmop is not supported on older hypervisors */
    xc_set_hvm_param(xc_handle, domid, HVM_PARAM_DM_DOMAIN, DOMID_SELF);
#endif
    xc_get_hvm_param(xc_handle, domid, HVM_PARAM_IOREQ_PFN, &ioreq_pfn);
    fprintf(logfile, "shared page at pfn %lx\n", ioreq_pfn);
    shared_page = xc_map_foreign_range(xc_handle, domid, XC_PAGE_SIZE,
                                       PROT_READ|PROT_WRITE, ioreq_pfn);
    if (shared_page == NULL) {
        fprintf(logfile, "map shared IO page returned error %d\n", errno);
        exit(-1);
    }

    xc_get_hvm_param(xc_handle, domid, HVM_PARAM_BUFIOREQ_PFN, &ioreq_pfn);
    fprintf(logfile, "buffered io page at pfn %lx\n", ioreq_pfn);
    buffered_io_page = xc_map_foreign_range(xc_handle, domid, XC_PAGE_SIZE,
                                            PROT_READ|PROT_WRITE, ioreq_pfn);
    if (buffered_io_page == NULL) {
        fprintf(logfile, "map buffered IO page returned error %d\n", errno);
        exit(-1);
    }

#if defined(__ia64__)
    xc_get_hvm_param(xc_handle, domid, HVM_PARAM_BUFPIOREQ_PFN, &ioreq_pfn);
    fprintf(logfile, "buffered pio page at pfn %lx\n", ioreq_pfn);
    buffered_pio_page = xc_map_foreign_range(xc_handle, domid, XC_PAGE_SIZE,
					     PROT_READ|PROT_WRITE, ioreq_pfn);
    if (buffered_pio_page == NULL) {
        fprintf(logfile, "map buffered PIO page returned error %d\n", errno);
        exit(-1);
    }

    nr_pages = ram_size / XC_PAGE_SIZE;

    page_array = (xen_pfn_t *)malloc(nr_pages * sizeof(xen_pfn_t));
    if (page_array == NULL) {
        fprintf(logfile, "malloc returned error %d\n", errno);
        exit(-1);
    }

    for (i = 0; i < nr_pages; i++)
        page_array[i] = i;

    /* VTI will not use memory between 3G~4G, so we just pass a legal pfn
       to make QEMU map continuous virtual memory space */
    if (ram_size > MMIO_START) {
        for (i = 0 ; i < (MEM_G >> XC_PAGE_SHIFT); i++)
            page_array[(MMIO_START >> XC_PAGE_SHIFT) + i] =
                (STORE_PAGE_START >> XC_PAGE_SHIFT); 
    }
    /* skipping VGA hole, same as above */
    if (ram_size > VGA_IO_START) {
        for (i = 0 ; i < (VGA_IO_SIZE >> XC_PAGE_SHIFT); i++)
            page_array[(VGA_IO_START >> XC_PAGE_SHIFT) + i] =
                (STORE_PAGE_START >> XC_PAGE_SHIFT); 
    }

    phys_ram_base = xc_map_foreign_pages(xc_handle, domid,
                                         PROT_READ|PROT_WRITE,
                                         page_array, nr_pages);
    if (phys_ram_base == 0) {
        fprintf(logfile, "xc_map_foreign_pages returned error %d\n", errno);
        exit(-1);
    }
    free(page_array);
#endif

    timeoffset_get();

	/* Initialize backend core & drivers */
    if (xen_be_init() != 0) {
        fprintf(stderr, "%s: xen backend core setup failed\n", __FUNCTION__);
        exit(1);
    }
    xen_be_register("console", &xen_console_ops);
    xen_be_register("vkbd", &xen_kbdmouse_ops);
#ifndef CONFIG_STUBDOM
    xen_be_register("qdisk", &xen_blkdev_ops);
#endif

    pc_machine.init(ram_size, vga_ram_size, boot_device,
		    kernel_filename, kernel_cmdline, initrd_filename,
		    cpu_model, direct_pci);
}

QEMUMachine xenfv_machine = {
    "xenfv",
    "Xen Fully-virtualized PC",
    xen_init_fv,
    BIOS_SIZE | RAMSIZE_FIXED,
    .max_cpus = 1,
    .nodisk_ok = 1,
};

/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
