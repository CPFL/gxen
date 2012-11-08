/*
 * Physical memory management
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "memory.h"
#include "exec-memory.h"
#include "ioport.h"
#include "bitops.h"
#include "kvm.h"
#include <assert.h>

unsigned memory_region_transaction_depth = 0;

typedef struct AddrRange AddrRange;

/*
 * Note using signed integers limits us to physical addresses at most
 * 63 bits wide.  They are needed for negative offsetting in aliases
 * (large MemoryRegion::alias_offset).
 */
struct AddrRange {
    Int128 start;
    Int128 size;
};

static AddrRange addrrange_make(Int128 start, Int128 size)
{
    return (AddrRange) { start, size };
}

static bool addrrange_equal(AddrRange r1, AddrRange r2)
{
    return int128_eq(r1.start, r2.start) && int128_eq(r1.size, r2.size);
}

static Int128 addrrange_end(AddrRange r)
{
    return int128_add(r.start, r.size);
}

static AddrRange addrrange_shift(AddrRange range, Int128 delta)
{
    int128_addto(&range.start, delta);
    return range;
}

static bool addrrange_contains(AddrRange range, Int128 addr)
{
    return int128_ge(addr, range.start)
        && int128_lt(addr, addrrange_end(range));
}

static bool addrrange_intersects(AddrRange r1, AddrRange r2)
{
    return addrrange_contains(r1, r2.start)
        || addrrange_contains(r2, r1.start);
}

static AddrRange addrrange_intersection(AddrRange r1, AddrRange r2)
{
    Int128 start = int128_max(r1.start, r2.start);
    Int128 end = int128_min(addrrange_end(r1), addrrange_end(r2));
    return addrrange_make(start, int128_sub(end, start));
}

struct CoalescedMemoryRange {
    AddrRange addr;
    QTAILQ_ENTRY(CoalescedMemoryRange) link;
};

struct MemoryRegionIoeventfd {
    AddrRange addr;
    bool match_data;
    uint64_t data;
    int fd;
};

static bool memory_region_ioeventfd_before(MemoryRegionIoeventfd a,
                                           MemoryRegionIoeventfd b)
{
    if (int128_lt(a.addr.start, b.addr.start)) {
        return true;
    } else if (int128_gt(a.addr.start, b.addr.start)) {
        return false;
    } else if (int128_lt(a.addr.size, b.addr.size)) {
        return true;
    } else if (int128_gt(a.addr.size, b.addr.size)) {
        return false;
    } else if (a.match_data < b.match_data) {
        return true;
    } else  if (a.match_data > b.match_data) {
        return false;
    } else if (a.match_data) {
        if (a.data < b.data) {
            return true;
        } else if (a.data > b.data) {
            return false;
        }
    }
    if (a.fd < b.fd) {
        return true;
    } else if (a.fd > b.fd) {
        return false;
    }
    return false;
}

static bool memory_region_ioeventfd_equal(MemoryRegionIoeventfd a,
                                          MemoryRegionIoeventfd b)
{
    return !memory_region_ioeventfd_before(a, b)
        && !memory_region_ioeventfd_before(b, a);
}

typedef struct FlatRange FlatRange;
typedef struct FlatView FlatView;

/* Range of memory in the global map.  Addresses are absolute. */
struct FlatRange {
    MemoryRegion *mr;
    target_phys_addr_t offset_in_region;
    AddrRange addr;
    uint8_t dirty_log_mask;
    bool readable;
    bool readonly;
};

/* Flattened global view of current active memory hierarchy.  Kept in sorted
 * order.
 */
struct FlatView {
    FlatRange *ranges;
    unsigned nr;
    unsigned nr_allocated;
};

typedef struct AddressSpace AddressSpace;
typedef struct AddressSpaceOps AddressSpaceOps;

/* A system address space - I/O, memory, etc. */
struct AddressSpace {
    const AddressSpaceOps *ops;
    MemoryRegion *root;
    FlatView current_map;
    int ioeventfd_nb;
    MemoryRegionIoeventfd *ioeventfds;
};

struct AddressSpaceOps {
    void (*range_add)(AddressSpace *as, FlatRange *fr);
    void (*range_del)(AddressSpace *as, FlatRange *fr);
    void (*log_start)(AddressSpace *as, FlatRange *fr);
    void (*log_stop)(AddressSpace *as, FlatRange *fr);
    void (*ioeventfd_add)(AddressSpace *as, MemoryRegionIoeventfd *fd);
    void (*ioeventfd_del)(AddressSpace *as, MemoryRegionIoeventfd *fd);
};

#define FOR_EACH_FLAT_RANGE(var, view)          \
    for (var = (view)->ranges; var < (view)->ranges + (view)->nr; ++var)

static bool flatrange_equal(FlatRange *a, FlatRange *b)
{
    return a->mr == b->mr
        && addrrange_equal(a->addr, b->addr)
        && a->offset_in_region == b->offset_in_region
        && a->readable == b->readable
        && a->readonly == b->readonly;
}

static void flatview_init(FlatView *view)
{
    view->ranges = NULL;
    view->nr = 0;
    view->nr_allocated = 0;
}

/* Insert a range into a given position.  Caller is responsible for maintaining
 * sorting order.
 */
static void flatview_insert(FlatView *view, unsigned pos, FlatRange *range)
{
    if (view->nr == view->nr_allocated) {
        view->nr_allocated = MAX(2 * view->nr, 10);
        view->ranges = g_realloc(view->ranges,
                                    view->nr_allocated * sizeof(*view->ranges));
    }
    memmove(view->ranges + pos + 1, view->ranges + pos,
            (view->nr - pos) * sizeof(FlatRange));
    view->ranges[pos] = *range;
    ++view->nr;
}

static void flatview_destroy(FlatView *view)
{
    g_free(view->ranges);
}

static bool can_merge(FlatRange *r1, FlatRange *r2)
{
    return int128_eq(addrrange_end(r1->addr), r2->addr.start)
        && r1->mr == r2->mr
        && int128_eq(int128_add(int128_make64(r1->offset_in_region),
                                r1->addr.size),
                     int128_make64(r2->offset_in_region))
        && r1->dirty_log_mask == r2->dirty_log_mask
        && r1->readable == r2->readable
        && r1->readonly == r2->readonly;
}

/* Attempt to simplify a view by merging ajacent ranges */
static void flatview_simplify(FlatView *view)
{
    unsigned i, j;

    i = 0;
    while (i < view->nr) {
        j = i + 1;
        while (j < view->nr
               && can_merge(&view->ranges[j-1], &view->ranges[j])) {
            int128_addto(&view->ranges[i].addr.size, view->ranges[j].addr.size);
            ++j;
        }
        ++i;
        memmove(&view->ranges[i], &view->ranges[j],
                (view->nr - j) * sizeof(view->ranges[j]));
        view->nr -= j - i;
    }
}

static void memory_region_read_accessor(void *opaque,
                                        target_phys_addr_t addr,
                                        uint64_t *value,
                                        unsigned size,
                                        unsigned shift,
                                        uint64_t mask)
{
    MemoryRegion *mr = opaque;
    uint64_t tmp;

    tmp = mr->ops->read(mr->opaque, addr, size);
    *value |= (tmp & mask) << shift;
}

static void memory_region_write_accessor(void *opaque,
                                         target_phys_addr_t addr,
                                         uint64_t *value,
                                         unsigned size,
                                         unsigned shift,
                                         uint64_t mask)
{
    MemoryRegion *mr = opaque;
    uint64_t tmp;

    tmp = (*value >> shift) & mask;
    mr->ops->write(mr->opaque, addr, tmp, size);
}

static void access_with_adjusted_size(target_phys_addr_t addr,
                                      uint64_t *value,
                                      unsigned size,
                                      unsigned access_size_min,
                                      unsigned access_size_max,
                                      void (*access)(void *opaque,
                                                     target_phys_addr_t addr,
                                                     uint64_t *value,
                                                     unsigned size,
                                                     unsigned shift,
                                                     uint64_t mask),
                                      void *opaque)
{
    uint64_t access_mask;
    unsigned access_size;
    unsigned i;

    if (!access_size_min) {
        access_size_min = 1;
    }
    if (!access_size_max) {
        access_size_max = 4;
    }
    access_size = MAX(MIN(size, access_size_max), access_size_min);
    access_mask = -1ULL >> (64 - access_size * 8);
    for (i = 0; i < size; i += access_size) {
        /* FIXME: big-endian support */
        access(opaque, addr + i, value, access_size, i * 8, access_mask);
    }
}

static void memory_region_prepare_ram_addr(MemoryRegion *mr);

static void as_memory_range_add(AddressSpace *as, FlatRange *fr)
{
    ram_addr_t phys_offset, region_offset;

    memory_region_prepare_ram_addr(fr->mr);

    phys_offset = fr->mr->ram_addr;
    region_offset = fr->offset_in_region;
    /* cpu_register_physical_memory_log() wants region_offset for
     * mmio, but prefers offseting phys_offset for RAM.  Humour it.
     */
    if ((phys_offset & ~TARGET_PAGE_MASK) <= IO_MEM_ROM) {
        phys_offset += region_offset;
        region_offset = 0;
    }

    if (!fr->readable) {
        phys_offset &= ~TARGET_PAGE_MASK & ~IO_MEM_ROMD;
    }

    if (fr->readonly) {
        phys_offset |= IO_MEM_ROM;
    }

    cpu_register_physical_memory_log(int128_get64(fr->addr.start),
                                     int128_get64(fr->addr.size),
                                     phys_offset,
                                     region_offset,
                                     fr->dirty_log_mask);
}

static void as_memory_range_del(AddressSpace *as, FlatRange *fr)
{
    if (fr->dirty_log_mask) {
        Int128 end = addrrange_end(fr->addr);
        cpu_physical_sync_dirty_bitmap(int128_get64(fr->addr.start),
                                       int128_get64(end));
    }
    cpu_register_physical_memory(int128_get64(fr->addr.start),
                                 int128_get64(fr->addr.size),
                                 IO_MEM_UNASSIGNED);
}

static void as_memory_log_start(AddressSpace *as, FlatRange *fr)
{
    cpu_physical_log_start(int128_get64(fr->addr.start),
                           int128_get64(fr->addr.size));
}

static void as_memory_log_stop(AddressSpace *as, FlatRange *fr)
{
    cpu_physical_log_stop(int128_get64(fr->addr.start),
                          int128_get64(fr->addr.size));
}

static void as_memory_ioeventfd_add(AddressSpace *as, MemoryRegionIoeventfd *fd)
{
    int r;

    assert(fd->match_data && int128_get64(fd->addr.size) == 4);

    r = kvm_set_ioeventfd_mmio_long(fd->fd, int128_get64(fd->addr.start),
                                    fd->data, true);
    if (r < 0) {
        abort();
    }
}

static void as_memory_ioeventfd_del(AddressSpace *as, MemoryRegionIoeventfd *fd)
{
    int r;

    r = kvm_set_ioeventfd_mmio_long(fd->fd, int128_get64(fd->addr.start),
                                    fd->data, false);
    if (r < 0) {
        abort();
    }
}

static const AddressSpaceOps address_space_ops_memory = {
    .range_add = as_memory_range_add,
    .range_del = as_memory_range_del,
    .log_start = as_memory_log_start,
    .log_stop = as_memory_log_stop,
    .ioeventfd_add = as_memory_ioeventfd_add,
    .ioeventfd_del = as_memory_ioeventfd_del,
};

static AddressSpace address_space_memory = {
    .ops = &address_space_ops_memory,
};

static const MemoryRegionPortio *find_portio(MemoryRegion *mr, uint64_t offset,
                                             unsigned width, bool write)
{
    const MemoryRegionPortio *mrp;

    for (mrp = mr->ops->old_portio; mrp->size; ++mrp) {
        if (offset >= mrp->offset && offset < mrp->offset + mrp->len
            && width == mrp->size
            && (write ? (bool)mrp->write : (bool)mrp->read)) {
            return mrp;
        }
    }
    return NULL;
}

static void memory_region_iorange_read(IORange *iorange,
                                       uint64_t offset,
                                       unsigned width,
                                       uint64_t *data)
{
    MemoryRegion *mr = container_of(iorange, MemoryRegion, iorange);

    if (mr->ops->old_portio) {
        const MemoryRegionPortio *mrp = find_portio(mr, offset, width, false);

        *data = ((uint64_t)1 << (width * 8)) - 1;
        if (mrp) {
            *data = mrp->read(mr->opaque, offset + mr->offset);
        } else if (width == 2) {
            mrp = find_portio(mr, offset, 1, false);
            assert(mrp);
            *data = mrp->read(mr->opaque, offset + mr->offset) |
                    (mrp->read(mr->opaque, offset + mr->offset + 1) << 8);
        }
        return;
    }
    *data = 0;
    access_with_adjusted_size(offset + mr->offset, data, width,
                              mr->ops->impl.min_access_size,
                              mr->ops->impl.max_access_size,
                              memory_region_read_accessor, mr);
}

static void memory_region_iorange_write(IORange *iorange,
                                        uint64_t offset,
                                        unsigned width,
                                        uint64_t data)
{
    MemoryRegion *mr = container_of(iorange, MemoryRegion, iorange);

    if (mr->ops->old_portio) {
        const MemoryRegionPortio *mrp = find_portio(mr, offset, width, true);

        if (mrp) {
            mrp->write(mr->opaque, offset + mr->offset, data);
        } else if (width == 2) {
            mrp = find_portio(mr, offset, 1, false);
            assert(mrp);
            mrp->write(mr->opaque, offset + mr->offset, data & 0xff);
            mrp->write(mr->opaque, offset + mr->offset + 1, data >> 8);
        }
        return;
    }
    access_with_adjusted_size(offset + mr->offset, &data, width,
                              mr->ops->impl.min_access_size,
                              mr->ops->impl.max_access_size,
                              memory_region_write_accessor, mr);
}

static const IORangeOps memory_region_iorange_ops = {
    .read = memory_region_iorange_read,
    .write = memory_region_iorange_write,
};

static void as_io_range_add(AddressSpace *as, FlatRange *fr)
{
    iorange_init(&fr->mr->iorange, &memory_region_iorange_ops,
                 int128_get64(fr->addr.start), int128_get64(fr->addr.size));
    ioport_register(&fr->mr->iorange);
}

static void as_io_range_del(AddressSpace *as, FlatRange *fr)
{
    isa_unassign_ioport(int128_get64(fr->addr.start),
                        int128_get64(fr->addr.size));
}

static void as_io_ioeventfd_add(AddressSpace *as, MemoryRegionIoeventfd *fd)
{
    int r;

    assert(fd->match_data && int128_get64(fd->addr.size) == 2);

    r = kvm_set_ioeventfd_pio_word(fd->fd, int128_get64(fd->addr.start),
                                   fd->data, true);
    if (r < 0) {
        abort();
    }
}

static void as_io_ioeventfd_del(AddressSpace *as, MemoryRegionIoeventfd *fd)
{
    int r;

    r = kvm_set_ioeventfd_pio_word(fd->fd, int128_get64(fd->addr.start),
                                   fd->data, false);
    if (r < 0) {
        abort();
    }
}

static const AddressSpaceOps address_space_ops_io = {
    .range_add = as_io_range_add,
    .range_del = as_io_range_del,
    .ioeventfd_add = as_io_ioeventfd_add,
    .ioeventfd_del = as_io_ioeventfd_del,
};

static AddressSpace address_space_io = {
    .ops = &address_space_ops_io,
};

/* Render a memory region into the global view.  Ranges in @view obscure
 * ranges in @mr.
 */
static void render_memory_region(FlatView *view,
                                 MemoryRegion *mr,
                                 Int128 base,
                                 AddrRange clip,
                                 bool readonly)
{
    MemoryRegion *subregion;
    unsigned i;
    target_phys_addr_t offset_in_region;
    Int128 remain;
    Int128 now;
    FlatRange fr;
    AddrRange tmp;

    int128_addto(&base, int128_make64(mr->addr));
    readonly |= mr->readonly;

    tmp = addrrange_make(base, mr->size);

    if (!addrrange_intersects(tmp, clip)) {
        return;
    }

    clip = addrrange_intersection(tmp, clip);

    if (mr->alias) {
        int128_subfrom(&base, int128_make64(mr->alias->addr));
        int128_subfrom(&base, int128_make64(mr->alias_offset));
        render_memory_region(view, mr->alias, base, clip, readonly);
        return;
    }

    /* Render subregions in priority order. */
    QTAILQ_FOREACH(subregion, &mr->subregions, subregions_link) {
        render_memory_region(view, subregion, base, clip, readonly);
    }

    if (!mr->terminates) {
        return;
    }

    offset_in_region = int128_get64(int128_sub(clip.start, base));
    base = clip.start;
    remain = clip.size;

    /* Render the region itself into any gaps left by the current view. */
    for (i = 0; i < view->nr && int128_nz(remain); ++i) {
        if (int128_ge(base, addrrange_end(view->ranges[i].addr))) {
            continue;
        }
        if (int128_lt(base, view->ranges[i].addr.start)) {
            now = int128_min(remain,
                             int128_sub(view->ranges[i].addr.start, base));
            fr.mr = mr;
            fr.offset_in_region = offset_in_region;
            fr.addr = addrrange_make(base, now);
            fr.dirty_log_mask = mr->dirty_log_mask;
            fr.readable = mr->readable;
            fr.readonly = readonly;
            flatview_insert(view, i, &fr);
            ++i;
            int128_addto(&base, now);
            offset_in_region += int128_get64(now);
            int128_subfrom(&remain, now);
        }
        if (int128_eq(base, view->ranges[i].addr.start)) {
            now = int128_min(remain, view->ranges[i].addr.size);
            int128_addto(&base, now);
            offset_in_region += int128_get64(now);
            int128_subfrom(&remain, now);
        }
    }
    if (int128_nz(remain)) {
        fr.mr = mr;
        fr.offset_in_region = offset_in_region;
        fr.addr = addrrange_make(base, remain);
        fr.dirty_log_mask = mr->dirty_log_mask;
        fr.readable = mr->readable;
        fr.readonly = readonly;
        flatview_insert(view, i, &fr);
    }
}

/* Render a memory topology into a list of disjoint absolute ranges. */
static FlatView generate_memory_topology(MemoryRegion *mr)
{
    FlatView view;

    flatview_init(&view);

    render_memory_region(&view, mr, int128_zero(),
                         addrrange_make(int128_zero(), int128_2_64()), false);
    flatview_simplify(&view);

    return view;
}

static void address_space_add_del_ioeventfds(AddressSpace *as,
                                             MemoryRegionIoeventfd *fds_new,
                                             unsigned fds_new_nb,
                                             MemoryRegionIoeventfd *fds_old,
                                             unsigned fds_old_nb)
{
    unsigned iold, inew;

    /* Generate a symmetric difference of the old and new fd sets, adding
     * and deleting as necessary.
     */

    iold = inew = 0;
    while (iold < fds_old_nb || inew < fds_new_nb) {
        if (iold < fds_old_nb
            && (inew == fds_new_nb
                || memory_region_ioeventfd_before(fds_old[iold],
                                                  fds_new[inew]))) {
            as->ops->ioeventfd_del(as, &fds_old[iold]);
            ++iold;
        } else if (inew < fds_new_nb
                   && (iold == fds_old_nb
                       || memory_region_ioeventfd_before(fds_new[inew],
                                                         fds_old[iold]))) {
            as->ops->ioeventfd_add(as, &fds_new[inew]);
            ++inew;
        } else {
            ++iold;
            ++inew;
        }
    }
}

static void address_space_update_ioeventfds(AddressSpace *as)
{
    FlatRange *fr;
    unsigned ioeventfd_nb = 0;
    MemoryRegionIoeventfd *ioeventfds = NULL;
    AddrRange tmp;
    unsigned i;

    FOR_EACH_FLAT_RANGE(fr, &as->current_map) {
        for (i = 0; i < fr->mr->ioeventfd_nb; ++i) {
            tmp = addrrange_shift(fr->mr->ioeventfds[i].addr,
                                  int128_sub(fr->addr.start,
                                             int128_make64(fr->offset_in_region)));
            if (addrrange_intersects(fr->addr, tmp)) {
                ++ioeventfd_nb;
                ioeventfds = g_realloc(ioeventfds,
                                          ioeventfd_nb * sizeof(*ioeventfds));
                ioeventfds[ioeventfd_nb-1] = fr->mr->ioeventfds[i];
                ioeventfds[ioeventfd_nb-1].addr = tmp;
            }
        }
    }

    address_space_add_del_ioeventfds(as, ioeventfds, ioeventfd_nb,
                                     as->ioeventfds, as->ioeventfd_nb);

    g_free(as->ioeventfds);
    as->ioeventfds = ioeventfds;
    as->ioeventfd_nb = ioeventfd_nb;
}

static void address_space_update_topology_pass(AddressSpace *as,
                                               FlatView old_view,
                                               FlatView new_view,
                                               bool adding)
{
    unsigned iold, inew;
    FlatRange *frold, *frnew;

    /* Generate a symmetric difference of the old and new memory maps.
     * Kill ranges in the old map, and instantiate ranges in the new map.
     */
    iold = inew = 0;
    while (iold < old_view.nr || inew < new_view.nr) {
        if (iold < old_view.nr) {
            frold = &old_view.ranges[iold];
        } else {
            frold = NULL;
        }
        if (inew < new_view.nr) {
            frnew = &new_view.ranges[inew];
        } else {
            frnew = NULL;
        }

        if (frold
            && (!frnew
                || int128_lt(frold->addr.start, frnew->addr.start)
                || (int128_eq(frold->addr.start, frnew->addr.start)
                    && !flatrange_equal(frold, frnew)))) {
            /* In old, but (not in new, or in new but attributes changed). */

            if (!adding) {
                as->ops->range_del(as, frold);
            }

            ++iold;
        } else if (frold && frnew && flatrange_equal(frold, frnew)) {
            /* In both (logging may have changed) */

            if (adding) {
                if (frold->dirty_log_mask && !frnew->dirty_log_mask) {
                    as->ops->log_stop(as, frnew);
                } else if (frnew->dirty_log_mask && !frold->dirty_log_mask) {
                    as->ops->log_start(as, frnew);
                }
            }

            ++iold;
            ++inew;
        } else {
            /* In new */

            if (adding) {
                as->ops->range_add(as, frnew);
            }

            ++inew;
        }
    }
}


static void address_space_update_topology(AddressSpace *as)
{
    FlatView old_view = as->current_map;
    FlatView new_view = generate_memory_topology(as->root);

    address_space_update_topology_pass(as, old_view, new_view, false);
    address_space_update_topology_pass(as, old_view, new_view, true);

    as->current_map = new_view;
    flatview_destroy(&old_view);
    address_space_update_ioeventfds(as);
}

static void memory_region_update_topology(void)
{
    if (memory_region_transaction_depth) {
        return;
    }

    if (address_space_memory.root) {
        address_space_update_topology(&address_space_memory);
    }
    if (address_space_io.root) {
        address_space_update_topology(&address_space_io);
    }
}

void memory_region_transaction_begin(void)
{
    ++memory_region_transaction_depth;
}

void memory_region_transaction_commit(void)
{
    assert(memory_region_transaction_depth);
    --memory_region_transaction_depth;
    memory_region_update_topology();
}

static void memory_region_destructor_none(MemoryRegion *mr)
{
}

static void memory_region_destructor_ram(MemoryRegion *mr)
{
    qemu_ram_free(mr->ram_addr);
}

static void memory_region_destructor_ram_from_ptr(MemoryRegion *mr)
{
    qemu_ram_free_from_ptr(mr->ram_addr);
}

static void memory_region_destructor_iomem(MemoryRegion *mr)
{
    cpu_unregister_io_memory(mr->ram_addr);
}

static void memory_region_destructor_rom_device(MemoryRegion *mr)
{
    qemu_ram_free(mr->ram_addr & TARGET_PAGE_MASK);
    cpu_unregister_io_memory(mr->ram_addr & ~(TARGET_PAGE_MASK | IO_MEM_ROMD));
}

void memory_region_init(MemoryRegion *mr,
                        const char *name,
                        uint64_t size)
{
    mr->ops = NULL;
    mr->parent = NULL;
    mr->size = int128_make64(size);
    if (size == UINT64_MAX) {
        mr->size = int128_2_64();
    }
    mr->addr = 0;
    mr->offset = 0;
    mr->terminates = false;
    mr->readable = true;
    mr->readonly = false;
    mr->destructor = memory_region_destructor_none;
    mr->priority = 0;
    mr->may_overlap = false;
    mr->alias = NULL;
    QTAILQ_INIT(&mr->subregions);
    memset(&mr->subregions_link, 0, sizeof mr->subregions_link);
    QTAILQ_INIT(&mr->coalesced);
    mr->name = g_strdup(name);
    mr->dirty_log_mask = 0;
    mr->ioeventfd_nb = 0;
    mr->ioeventfds = NULL;
}

static bool memory_region_access_valid(MemoryRegion *mr,
                                       target_phys_addr_t addr,
                                       unsigned size)
{
    if (!mr->ops->valid.unaligned && (addr & (size - 1))) {
        return false;
    }

    /* Treat zero as compatibility all valid */
    if (!mr->ops->valid.max_access_size) {
        return true;
    }

    if (size > mr->ops->valid.max_access_size
        || size < mr->ops->valid.min_access_size) {
        return false;
    }
    return true;
}

static uint32_t memory_region_read_thunk_n(void *_mr,
                                           target_phys_addr_t addr,
                                           unsigned size)
{
    MemoryRegion *mr = _mr;
    uint64_t data = 0;

    if (!memory_region_access_valid(mr, addr, size)) {
        return -1U; /* FIXME: better signalling */
    }

    if (!mr->ops->read) {
        return mr->ops->old_mmio.read[bitops_ffsl(size)](mr->opaque, addr);
    }

    /* FIXME: support unaligned access */
    access_with_adjusted_size(addr + mr->offset, &data, size,
                              mr->ops->impl.min_access_size,
                              mr->ops->impl.max_access_size,
                              memory_region_read_accessor, mr);

    return data;
}

static void memory_region_write_thunk_n(void *_mr,
                                        target_phys_addr_t addr,
                                        unsigned size,
                                        uint64_t data)
{
    MemoryRegion *mr = _mr;

    if (!memory_region_access_valid(mr, addr, size)) {
        return; /* FIXME: better signalling */
    }

    if (!mr->ops->write) {
        mr->ops->old_mmio.write[bitops_ffsl(size)](mr->opaque, addr, data);
        return;
    }

    /* FIXME: support unaligned access */
    access_with_adjusted_size(addr + mr->offset, &data, size,
                              mr->ops->impl.min_access_size,
                              mr->ops->impl.max_access_size,
                              memory_region_write_accessor, mr);
}

static uint32_t memory_region_read_thunk_b(void *mr, target_phys_addr_t addr)
{
    return memory_region_read_thunk_n(mr, addr, 1);
}

static uint32_t memory_region_read_thunk_w(void *mr, target_phys_addr_t addr)
{
    return memory_region_read_thunk_n(mr, addr, 2);
}

static uint32_t memory_region_read_thunk_l(void *mr, target_phys_addr_t addr)
{
    return memory_region_read_thunk_n(mr, addr, 4);
}

static void memory_region_write_thunk_b(void *mr, target_phys_addr_t addr,
                                        uint32_t data)
{
    memory_region_write_thunk_n(mr, addr, 1, data);
}

static void memory_region_write_thunk_w(void *mr, target_phys_addr_t addr,
                                        uint32_t data)
{
    memory_region_write_thunk_n(mr, addr, 2, data);
}

static void memory_region_write_thunk_l(void *mr, target_phys_addr_t addr,
                                        uint32_t data)
{
    memory_region_write_thunk_n(mr, addr, 4, data);
}

static CPUReadMemoryFunc * const memory_region_read_thunk[] = {
    memory_region_read_thunk_b,
    memory_region_read_thunk_w,
    memory_region_read_thunk_l,
};

static CPUWriteMemoryFunc * const memory_region_write_thunk[] = {
    memory_region_write_thunk_b,
    memory_region_write_thunk_w,
    memory_region_write_thunk_l,
};

static void memory_region_prepare_ram_addr(MemoryRegion *mr)
{
    if (mr->backend_registered) {
        return;
    }

    mr->destructor = memory_region_destructor_iomem;
    mr->ram_addr = cpu_register_io_memory(memory_region_read_thunk,
                                          memory_region_write_thunk,
                                          mr,
                                          mr->ops->endianness);
    mr->backend_registered = true;
}

void memory_region_init_io(MemoryRegion *mr,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->ops = ops;
    mr->opaque = opaque;
    mr->terminates = true;
    mr->backend_registered = false;
}

void memory_region_init_ram(MemoryRegion *mr,
                            DeviceState *dev,
                            const char *name,
                            uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->terminates = true;
    mr->destructor = memory_region_destructor_ram;
    mr->ram_addr = qemu_ram_alloc(dev, name, size);
    mr->backend_registered = true;
}

void memory_region_init_ram_ptr(MemoryRegion *mr,
                                DeviceState *dev,
                                const char *name,
                                uint64_t size,
                                void *ptr)
{
    memory_region_init(mr, name, size);
    mr->terminates = true;
    mr->destructor = memory_region_destructor_ram_from_ptr;
    mr->ram_addr = qemu_ram_alloc_from_ptr(dev, name, size, ptr);
    mr->backend_registered = true;
}

void memory_region_init_alias(MemoryRegion *mr,
                              const char *name,
                              MemoryRegion *orig,
                              target_phys_addr_t offset,
                              uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->alias = orig;
    mr->alias_offset = offset;
}

void memory_region_init_rom_device(MemoryRegion *mr,
                                   const MemoryRegionOps *ops,
                                   void *opaque,
                                   DeviceState *dev,
                                   const char *name,
                                   uint64_t size)
{
    memory_region_init(mr, name, size);
    mr->ops = ops;
    mr->opaque = opaque;
    mr->terminates = true;
    mr->destructor = memory_region_destructor_rom_device;
    mr->ram_addr = qemu_ram_alloc(dev, name, size);
    mr->ram_addr |= cpu_register_io_memory(memory_region_read_thunk,
                                           memory_region_write_thunk,
                                           mr,
                                           mr->ops->endianness);
    mr->ram_addr |= IO_MEM_ROMD;
    mr->backend_registered = true;
}

void memory_region_destroy(MemoryRegion *mr)
{
    assert(QTAILQ_EMPTY(&mr->subregions));
    mr->destructor(mr);
    memory_region_clear_coalescing(mr);
    g_free((char *)mr->name);
    g_free(mr->ioeventfds);
}

uint64_t memory_region_size(MemoryRegion *mr)
{
    if (int128_eq(mr->size, int128_2_64())) {
        return UINT64_MAX;
    }
    return int128_get64(mr->size);
}

void memory_region_set_offset(MemoryRegion *mr, target_phys_addr_t offset)
{
    mr->offset = offset;
}

void memory_region_set_log(MemoryRegion *mr, bool log, unsigned client)
{
    uint8_t mask = 1 << client;

    mr->dirty_log_mask = (mr->dirty_log_mask & ~mask) | (log * mask);
    memory_region_update_topology();
}

bool memory_region_get_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                             unsigned client)
{
    assert(mr->terminates);
    return cpu_physical_memory_get_dirty(mr->ram_addr + addr, 1 << client);
}

void memory_region_set_dirty(MemoryRegion *mr, target_phys_addr_t addr)
{
    assert(mr->terminates);
    return cpu_physical_memory_set_dirty(mr->ram_addr + addr);
}

void memory_region_sync_dirty_bitmap(MemoryRegion *mr)
{
    FlatRange *fr;

    FOR_EACH_FLAT_RANGE(fr, &address_space_memory.current_map) {
        if (fr->mr == mr) {
            cpu_physical_sync_dirty_bitmap(int128_get64(fr->addr.start),
                                           int128_get64(addrrange_end(fr->addr)));
        }
    }
}

void memory_region_set_readonly(MemoryRegion *mr, bool readonly)
{
    if (mr->readonly != readonly) {
        mr->readonly = readonly;
        memory_region_update_topology();
    }
}

void memory_region_rom_device_set_readable(MemoryRegion *mr, bool readable)
{
    if (mr->readable != readable) {
        mr->readable = readable;
        memory_region_update_topology();
    }
}

void memory_region_reset_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                               target_phys_addr_t size, unsigned client)
{
    assert(mr->terminates);
    cpu_physical_memory_reset_dirty(mr->ram_addr + addr,
                                    mr->ram_addr + addr + size,
                                    1 << client);
}

void *memory_region_get_ram_ptr(MemoryRegion *mr)
{
    if (mr->alias) {
        return memory_region_get_ram_ptr(mr->alias) + mr->alias_offset;
    }

    assert(mr->terminates);

    return qemu_get_ram_ptr(mr->ram_addr & TARGET_PAGE_MASK);
}

static void memory_region_update_coalesced_range(MemoryRegion *mr)
{
    FlatRange *fr;
    CoalescedMemoryRange *cmr;
    AddrRange tmp;

    FOR_EACH_FLAT_RANGE(fr, &address_space_memory.current_map) {
        if (fr->mr == mr) {
            qemu_unregister_coalesced_mmio(int128_get64(fr->addr.start),
                                           int128_get64(fr->addr.size));
            QTAILQ_FOREACH(cmr, &mr->coalesced, link) {
                tmp = addrrange_shift(cmr->addr,
                                      int128_sub(fr->addr.start,
                                                 int128_make64(fr->offset_in_region)));
                if (!addrrange_intersects(tmp, fr->addr)) {
                    continue;
                }
                tmp = addrrange_intersection(tmp, fr->addr);
                qemu_register_coalesced_mmio(int128_get64(tmp.start),
                                             int128_get64(tmp.size));
            }
        }
    }
}

void memory_region_set_coalescing(MemoryRegion *mr)
{
    memory_region_clear_coalescing(mr);
    memory_region_add_coalescing(mr, 0, int128_get64(mr->size));
}

void memory_region_add_coalescing(MemoryRegion *mr,
                                  target_phys_addr_t offset,
                                  uint64_t size)
{
    CoalescedMemoryRange *cmr = g_malloc(sizeof(*cmr));

    cmr->addr = addrrange_make(int128_make64(offset), int128_make64(size));
    QTAILQ_INSERT_TAIL(&mr->coalesced, cmr, link);
    memory_region_update_coalesced_range(mr);
}

void memory_region_clear_coalescing(MemoryRegion *mr)
{
    CoalescedMemoryRange *cmr;

    while (!QTAILQ_EMPTY(&mr->coalesced)) {
        cmr = QTAILQ_FIRST(&mr->coalesced);
        QTAILQ_REMOVE(&mr->coalesced, cmr, link);
        g_free(cmr);
    }
    memory_region_update_coalesced_range(mr);
}

void memory_region_add_eventfd(MemoryRegion *mr,
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd)
{
    MemoryRegionIoeventfd mrfd = {
        .addr.start = int128_make64(addr),
        .addr.size = int128_make64(size),
        .match_data = match_data,
        .data = data,
        .fd = fd,
    };
    unsigned i;

    for (i = 0; i < mr->ioeventfd_nb; ++i) {
        if (memory_region_ioeventfd_before(mrfd, mr->ioeventfds[i])) {
            break;
        }
    }
    ++mr->ioeventfd_nb;
    mr->ioeventfds = g_realloc(mr->ioeventfds,
                                  sizeof(*mr->ioeventfds) * mr->ioeventfd_nb);
    memmove(&mr->ioeventfds[i+1], &mr->ioeventfds[i],
            sizeof(*mr->ioeventfds) * (mr->ioeventfd_nb-1 - i));
    mr->ioeventfds[i] = mrfd;
    memory_region_update_topology();
}

void memory_region_del_eventfd(MemoryRegion *mr,
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd)
{
    MemoryRegionIoeventfd mrfd = {
        .addr.start = int128_make64(addr),
        .addr.size = int128_make64(size),
        .match_data = match_data,
        .data = data,
        .fd = fd,
    };
    unsigned i;

    for (i = 0; i < mr->ioeventfd_nb; ++i) {
        if (memory_region_ioeventfd_equal(mrfd, mr->ioeventfds[i])) {
            break;
        }
    }
    assert(i != mr->ioeventfd_nb);
    memmove(&mr->ioeventfds[i], &mr->ioeventfds[i+1],
            sizeof(*mr->ioeventfds) * (mr->ioeventfd_nb - (i+1)));
    --mr->ioeventfd_nb;
    mr->ioeventfds = g_realloc(mr->ioeventfds,
                                  sizeof(*mr->ioeventfds)*mr->ioeventfd_nb + 1);
    memory_region_update_topology();
}

static void memory_region_add_subregion_common(MemoryRegion *mr,
                                               target_phys_addr_t offset,
                                               MemoryRegion *subregion)
{
    MemoryRegion *other;

    assert(!subregion->parent);
    subregion->parent = mr;
    subregion->addr = offset;
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->may_overlap || other->may_overlap) {
            continue;
        }
        if (int128_gt(int128_make64(offset),
                      int128_add(int128_make64(other->addr), other->size))
            || int128_le(int128_add(int128_make64(offset), subregion->size),
                         int128_make64(other->addr))) {
            continue;
        }
#if 0
        printf("warning: subregion collision %llx/%llx (%s) "
               "vs %llx/%llx (%s)\n",
               (unsigned long long)offset,
               (unsigned long long)int128_get64(subregion->size),
               subregion->name,
               (unsigned long long)other->addr,
               (unsigned long long)int128_get64(other->size),
               other->name);
#endif
    }
    QTAILQ_FOREACH(other, &mr->subregions, subregions_link) {
        if (subregion->priority >= other->priority) {
            QTAILQ_INSERT_BEFORE(other, subregion, subregions_link);
            goto done;
        }
    }
    QTAILQ_INSERT_TAIL(&mr->subregions, subregion, subregions_link);
done:
    memory_region_update_topology();
}


void memory_region_add_subregion(MemoryRegion *mr,
                                 target_phys_addr_t offset,
                                 MemoryRegion *subregion)
{
    subregion->may_overlap = false;
    subregion->priority = 0;
    memory_region_add_subregion_common(mr, offset, subregion);
}

void memory_region_add_subregion_overlap(MemoryRegion *mr,
                                         target_phys_addr_t offset,
                                         MemoryRegion *subregion,
                                         unsigned priority)
{
    subregion->may_overlap = true;
    subregion->priority = priority;
    memory_region_add_subregion_common(mr, offset, subregion);
}

void memory_region_del_subregion(MemoryRegion *mr,
                                 MemoryRegion *subregion)
{
    assert(subregion->parent == mr);
    subregion->parent = NULL;
    QTAILQ_REMOVE(&mr->subregions, subregion, subregions_link);
    memory_region_update_topology();
}

void set_system_memory_map(MemoryRegion *mr)
{
    address_space_memory.root = mr;
    memory_region_update_topology();
}

void set_system_io_map(MemoryRegion *mr)
{
    address_space_io.root = mr;
    memory_region_update_topology();
}

typedef struct MemoryRegionList MemoryRegionList;

struct MemoryRegionList {
    const MemoryRegion *mr;
    bool printed;
    QTAILQ_ENTRY(MemoryRegionList) queue;
};

typedef QTAILQ_HEAD(queue, MemoryRegionList) MemoryRegionListHead;

static void mtree_print_mr(fprintf_function mon_printf, void *f,
                           const MemoryRegion *mr, unsigned int level,
                           target_phys_addr_t base,
                           MemoryRegionListHead *alias_print_queue)
{
    MemoryRegionList *new_ml, *ml, *next_ml;
    MemoryRegionListHead submr_print_queue;
    const MemoryRegion *submr;
    unsigned int i;

    if (!mr) {
        return;
    }

    for (i = 0; i < level; i++) {
        mon_printf(f, "  ");
    }

    if (mr->alias) {
        MemoryRegionList *ml;
        bool found = false;

        /* check if the alias is already in the queue */
        QTAILQ_FOREACH(ml, alias_print_queue, queue) {
            if (ml->mr == mr->alias && !ml->printed) {
                found = true;
            }
        }

        if (!found) {
            ml = g_new(MemoryRegionList, 1);
            ml->mr = mr->alias;
            ml->printed = false;
            QTAILQ_INSERT_TAIL(alias_print_queue, ml, queue);
        }
        mon_printf(f, TARGET_FMT_plx "-" TARGET_FMT_plx " (prio %d): alias %s @%s "
                   TARGET_FMT_plx "-" TARGET_FMT_plx "\n",
                   base + mr->addr,
                   base + mr->addr
                   + (target_phys_addr_t)int128_get64(mr->size) - 1,
                   mr->priority,
                   mr->name,
                   mr->alias->name,
                   mr->alias_offset,
                   mr->alias_offset
                   + (target_phys_addr_t)int128_get64(mr->size) - 1);
    } else {
        mon_printf(f, TARGET_FMT_plx "-" TARGET_FMT_plx " (prio %d): %s\n",
                   base + mr->addr,
                   base + mr->addr
                   + (target_phys_addr_t)int128_get64(mr->size) - 1,
                   mr->priority,
                   mr->name);
    }

    QTAILQ_INIT(&submr_print_queue);

    QTAILQ_FOREACH(submr, &mr->subregions, subregions_link) {
        new_ml = g_new(MemoryRegionList, 1);
        new_ml->mr = submr;
        QTAILQ_FOREACH(ml, &submr_print_queue, queue) {
            if (new_ml->mr->addr < ml->mr->addr ||
                (new_ml->mr->addr == ml->mr->addr &&
                 new_ml->mr->priority > ml->mr->priority)) {
                QTAILQ_INSERT_BEFORE(ml, new_ml, queue);
                new_ml = NULL;
                break;
            }
        }
        if (new_ml) {
            QTAILQ_INSERT_TAIL(&submr_print_queue, new_ml, queue);
        }
    }

    QTAILQ_FOREACH(ml, &submr_print_queue, queue) {
        mtree_print_mr(mon_printf, f, ml->mr, level + 1, base + mr->addr,
                       alias_print_queue);
    }

    QTAILQ_FOREACH_SAFE(ml, &submr_print_queue, queue, next_ml) {
        g_free(ml);
    }
}

void mtree_info(fprintf_function mon_printf, void *f)
{
    MemoryRegionListHead ml_head;
    MemoryRegionList *ml, *ml2;

    QTAILQ_INIT(&ml_head);

    mon_printf(f, "memory\n");
    mtree_print_mr(mon_printf, f, address_space_memory.root, 0, 0, &ml_head);

    /* print aliased regions */
    QTAILQ_FOREACH(ml, &ml_head, queue) {
        if (!ml->printed) {
            mon_printf(f, "%s\n", ml->mr->name);
            mtree_print_mr(mon_printf, f, ml->mr, 0, 0, &ml_head);
        }
    }

    QTAILQ_FOREACH_SAFE(ml, &ml_head, queue, ml2) {
        g_free(ml);
    }

    if (address_space_io.root &&
        !QTAILQ_EMPTY(&address_space_io.root->subregions)) {
        QTAILQ_INIT(&ml_head);
        mon_printf(f, "I/O\n");
        mtree_print_mr(mon_printf, f, address_space_io.root, 0, 0, &ml_head);
    }
}
