/*
 * Physical memory management API
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

#ifndef MEMORY_H
#define MEMORY_H

#ifndef CONFIG_USER_ONLY

#include <stdint.h>
#include <stdbool.h>
#include "qemu-common.h"
#include "cpu-common.h"
#include "targphys.h"
#include "qemu-queue.h"
#include "iorange.h"
#include "ioport.h"
#include "int128.h"

typedef struct MemoryRegionOps MemoryRegionOps;
typedef struct MemoryRegion MemoryRegion;
typedef struct MemoryRegionPortio MemoryRegionPortio;
typedef struct MemoryRegionMmio MemoryRegionMmio;

/* Must match *_DIRTY_FLAGS in cpu-all.h.  To be replaced with dynamic
 * registration.
 */
#define DIRTY_MEMORY_VGA       0
#define DIRTY_MEMORY_CODE      1
#define DIRTY_MEMORY_MIGRATION 3

struct MemoryRegionMmio {
    CPUReadMemoryFunc *read[3];
    CPUWriteMemoryFunc *write[3];
};

/*
 * Memory region callbacks
 */
struct MemoryRegionOps {
    /* Read from the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    uint64_t (*read)(void *opaque,
                     target_phys_addr_t addr,
                     unsigned size);
    /* Write to the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    void (*write)(void *opaque,
                  target_phys_addr_t addr,
                  uint64_t data,
                  unsigned size);

    enum device_endian endianness;
    /* Guest-visible constraints: */
    struct {
        /* If nonzero, specify bounds on access sizes beyond which a machine
         * check is thrown.
         */
        unsigned min_access_size;
        unsigned max_access_size;
        /* If true, unaligned accesses are supported.  Otherwise unaligned
         * accesses throw machine checks.
         */
         bool unaligned;
    } valid;
    /* Internal implementation constraints: */
    struct {
        /* If nonzero, specifies the minimum size implemented.  Smaller sizes
         * will be rounded upwards and a partial result will be returned.
         */
        unsigned min_access_size;
        /* If nonzero, specifies the maximum size implemented.  Larger sizes
         * will be done as a series of accesses with smaller sizes.
         */
        unsigned max_access_size;
        /* If true, unaligned accesses are supported.  Otherwise all accesses
         * are converted to (possibly multiple) naturally aligned accesses.
         */
         bool unaligned;
    } impl;

    /* If .read and .write are not present, old_portio may be used for
     * backwards compatibility with old portio registration
     */
    const MemoryRegionPortio *old_portio;
    /* If .read and .write are not present, old_mmio may be used for
     * backwards compatibility with old mmio registration
     */
    const MemoryRegionMmio old_mmio;
};

typedef struct CoalescedMemoryRange CoalescedMemoryRange;
typedef struct MemoryRegionIoeventfd MemoryRegionIoeventfd;

struct MemoryRegion {
    /* All fields are private - violators will be prosecuted */
    const MemoryRegionOps *ops;
    void *opaque;
    MemoryRegion *parent;
    Int128 size;
    target_phys_addr_t addr;
    target_phys_addr_t offset;
    bool backend_registered;
    void (*destructor)(MemoryRegion *mr);
    ram_addr_t ram_addr;
    IORange iorange;
    bool terminates;
    bool readable;
    bool readonly; /* For RAM regions */
    MemoryRegion *alias;
    target_phys_addr_t alias_offset;
    unsigned priority;
    bool may_overlap;
    QTAILQ_HEAD(subregions, MemoryRegion) subregions;
    QTAILQ_ENTRY(MemoryRegion) subregions_link;
    QTAILQ_HEAD(coalesced_ranges, CoalescedMemoryRange) coalesced;
    const char *name;
    uint8_t dirty_log_mask;
    unsigned ioeventfd_nb;
    MemoryRegionIoeventfd *ioeventfds;
};

struct MemoryRegionPortio {
    uint32_t offset;
    uint32_t len;
    unsigned size;
    IOPortReadFunc *read;
    IOPortWriteFunc *write;
};

#define PORTIO_END_OF_LIST() { }

/**
 * memory_region_init: Initialize a memory region
 *
 * The region typically acts as a container for other memory regions.  Us
 * memory_region_add_subregion() to add subregions.
 *
 * @mr: the #MemoryRegion to be initialized
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region; any subregions beyond this size will be clipped
 */
void memory_region_init(MemoryRegion *mr,
                        const char *name,
                        uint64_t size);
/**
 * memory_region_init_io: Initialize an I/O memory region.
 *
 * Accesses into the region will be cause the callbacks in @ops to be called.
 * if @size is nonzero, subregions will be clipped to @size.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @ops: a structure containing read and write callbacks to be used when
 *       I/O is performed on the region.
 * @opaque: passed to to the read and write callbacks of the @ops structure.
 * @name: used for debugging; not visible to the user or ABI
 * @size: size of the region.
 */
void memory_region_init_io(MemoryRegion *mr,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size);

/**
 * memory_region_init_ram:  Initialize RAM memory region.  Accesses into the
 *                          region will be modify memory directly.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @dev: a device associated with the region; may be %NULL.
 * @name: the name of the region; the pair (@dev, @name) must be globally
 *        unique.  The name is part of the save/restore ABI and so cannot be
 *        changed.
 * @size: size of the region.
 */
void memory_region_init_ram(MemoryRegion *mr,
                            DeviceState *dev, /* FIXME: layering violation */
                            const char *name,
                            uint64_t size);

/**
 * memory_region_init_ram:  Initialize RAM memory region from a user-provided.
 *                          pointer.  Accesses into the region will be modify
 *                          memory directly.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @dev: a device associated with the region; may be %NULL.
 * @name: the name of the region; the pair (@dev, @name) must be globally
 *        unique.  The name is part of the save/restore ABI and so cannot be
 *        changed.
 * @size: size of the region.
 * @ptr: memory to be mapped; must contain at least @size bytes.
 */
void memory_region_init_ram_ptr(MemoryRegion *mr,
                                DeviceState *dev, /* FIXME: layering violation */
                                const char *name,
                                uint64_t size,
                                void *ptr);

/**
 * memory_region_init_alias: Initialize a memory region that aliases all or a
 *                           part of another memory region.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @name: used for debugging; not visible to the user or ABI
 * @orig: the region to be referenced; @mr will be equivalent to
 *        @orig between @offset and @offset + @size - 1.
 * @offset: start of the section in @orig to be referenced.
 * @size: size of the region.
 */
void memory_region_init_alias(MemoryRegion *mr,
                              const char *name,
                              MemoryRegion *orig,
                              target_phys_addr_t offset,
                              uint64_t size);

/**
 * memory_region_init_rom_device:  Initialize a ROM memory region.  Writes are
 *                                 handled via callbacks.
 *
 * @mr: the #MemoryRegion to be initialized.
 * @ops: callbacks for write access handling.
 * @dev: a device associated with the region; may be %NULL.
 * @name: the name of the region; the pair (@dev, @name) must be globally
 *        unique.  The name is part of the save/restore ABI and so cannot be
 *        changed.
 * @size: size of the region.
 */
void memory_region_init_rom_device(MemoryRegion *mr,
                                   const MemoryRegionOps *ops,
                                   void *opaque,
                                   DeviceState *dev, /* FIXME: layering violation */
                                   const char *name,
                                   uint64_t size);

/**
 * memory_region_destroy: Destroy a memory region and relaim all resources.
 *
 * @mr: the region to be destroyed.  May not currently be a subregion
 *      (see memory_region_add_subregion()) or referenced in an alias
 *      (see memory_region_init_alias()).
 */
void memory_region_destroy(MemoryRegion *mr);

/**
 * memory_region_size: get a memory region's size.
 *
 * @mr: the memory region being queried.
 */
uint64_t memory_region_size(MemoryRegion *mr);

/**
 * memory_region_get_ram_ptr: Get a pointer into a RAM memory region.
 *
 * Returns a host pointer to a RAM memory region (created with
 * memory_region_init_ram() or memory_region_init_ram_ptr()).  Use with
 * care.
 *
 * @mr: the memory region being queried.
 */
void *memory_region_get_ram_ptr(MemoryRegion *mr);

/**
 * memory_region_set_offset: Sets an offset to be added to MemoryRegionOps
 *                           callbacks.
 *
 * This function is deprecated and should not be used in new code.
 */
void memory_region_set_offset(MemoryRegion *mr, target_phys_addr_t offset);

/**
 * memory_region_set_log: Turn dirty logging on or off for a region.
 *
 * Turns dirty logging on or off for a specified client (display, migration).
 * Only meaningful for RAM regions.
 *
 * @mr: the memory region being updated.
 * @log: whether dirty logging is to be enabled or disabled.
 * @client: the user of the logging information; %DIRTY_MEMORY_MIGRATION or
 *          %DIRTY_MEMORY_VGA.
 */
void memory_region_set_log(MemoryRegion *mr, bool log, unsigned client);

/**
 * memory_region_get_dirty: Check whether a page is dirty for a specified
 *                          client.
 *
 * Checks whether a page has been written to since the last
 * call to memory_region_reset_dirty() with the same @client.  Dirty logging
 * must be enabled.
 *
 * @mr: the memory region being queried.
 * @addr: the address (relative to the start of the region) being queried.
 * @client: the user of the logging information; %DIRTY_MEMORY_MIGRATION or
 *          %DIRTY_MEMORY_VGA.
 */
bool memory_region_get_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                             unsigned client);

/**
 * memory_region_set_dirty: Mark a page as dirty in a memory region.
 *
 * Marks a page as dirty, after it has been dirtied outside guest code.
 *
 * @mr: the memory region being queried.
 * @addr: the address (relative to the start of the region) being dirtied.
 */
void memory_region_set_dirty(MemoryRegion *mr, target_phys_addr_t addr);

/**
 * memory_region_sync_dirty_bitmap: Synchronize a region's dirty bitmap with
 *                                  any external TLBs (e.g. kvm)
 *
 * Flushes dirty information from accelerators such as kvm and vhost-net
 * and makes it available to users of the memory API.
 *
 * @mr: the region being flushed.
 */
void memory_region_sync_dirty_bitmap(MemoryRegion *mr);

/**
 * memory_region_reset_dirty: Mark a range of pages as clean, for a specified
 *                            client.
 *
 * Marks a range of pages as no longer dirty.
 *
 * @mr: the region being updated.
 * @addr: the start of the subrange being cleaned.
 * @size: the size of the subrange being cleaned.
 * @client: the user of the logging information; %DIRTY_MEMORY_MIGRATION or
 *          %DIRTY_MEMORY_VGA.
 */
void memory_region_reset_dirty(MemoryRegion *mr, target_phys_addr_t addr,
                               target_phys_addr_t size, unsigned client);

/**
 * memory_region_set_readonly: Turn a memory region read-only (or read-write)
 *
 * Allows a memory region to be marked as read-only (turning it into a ROM).
 * only useful on RAM regions.
 *
 * @mr: the region being updated.
 * @readonly: whether rhe region is to be ROM or RAM.
 */
void memory_region_set_readonly(MemoryRegion *mr, bool readonly);

/**
 * memory_region_rom_device_set_readable: enable/disable ROM readability
 *
 * Allows a ROM device (initialized with memory_region_init_rom_device() to
 * to be marked as readable (default) or not readable.  When it is readable,
 * the device is mapped to guest memory.  When not readable, reads are
 * forwarded to the #MemoryRegion.read function.
 *
 * @mr: the memory region to be updated
 * @readable: whether reads are satisified directly (%true) or via callbacks
 *            (%false)
 */
void memory_region_rom_device_set_readable(MemoryRegion *mr, bool readable);

/**
 * memory_region_set_coalescing: Enable memory coalescing for the region.
 *
 * Enabled writes to a region to be queued for later processing. MMIO ->write
 * callbacks may be delayed until a non-coalesced MMIO is issued.
 * Only useful for IO regions.  Roughly similar to write-combining hardware.
 *
 * @mr: the memory region to be write coalesced
 */
void memory_region_set_coalescing(MemoryRegion *mr);

/**
 * memory_region_add_coalescing: Enable memory coalescing for a sub-range of
 *                               a region.
 *
 * Like memory_region_set_coalescing(), but works on a sub-range of a region.
 * Multiple calls can be issued coalesced disjoint ranges.
 *
 * @mr: the memory region to be updated.
 * @offset: the start of the range within the region to be coalesced.
 * @size: the size of the subrange to be coalesced.
 */
void memory_region_add_coalescing(MemoryRegion *mr,
                                  target_phys_addr_t offset,
                                  uint64_t size);

/**
 * memory_region_clear_coalescing: Disable MMIO coalescing for the region.
 *
 * Disables any coalescing caused by memory_region_set_coalescing() or
 * memory_region_add_coalescing().  Roughly equivalent to uncacheble memory
 * hardware.
 *
 * @mr: the memory region to be updated.
 */
void memory_region_clear_coalescing(MemoryRegion *mr);

/**
 * memory_region_add_eventfd: Request an eventfd to be triggered when a word
 *                            is written to a location.
 *
 * Marks a word in an IO region (initialized with memory_region_init_io())
 * as a trigger for an eventfd event.  The I/O callback will not be called.
 * The caller must be prepared to handle failure (hat is, take the required
 * action if the callback _is_ called).
 *
 * @mr: the memory region being updated.
 * @addr: the address within @mr that is to be monitored
 * @size: the size of the access to trigger the eventfd
 * @match_data: whether to match against @data, instead of just @addr
 * @data: the data to match against the guest write
 * @fd: the eventfd to be triggered when @addr, @size, and @data all match.
 **/
void memory_region_add_eventfd(MemoryRegion *mr,
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd);

/**
 * memory_region_del_eventfd: Cancel and eventfd.
 *
 * Cancels an eventfd trigger request by a previous memory_region_add_eventfd()
 * call.
 *
 * @mr: the memory region being updated.
 * @addr: the address within @mr that is to be monitored
 * @size: the size of the access to trigger the eventfd
 * @match_data: whether to match against @data, instead of just @addr
 * @data: the data to match against the guest write
 * @fd: the eventfd to be triggered when @addr, @size, and @data all match.
 */
void memory_region_del_eventfd(MemoryRegion *mr,
                               target_phys_addr_t addr,
                               unsigned size,
                               bool match_data,
                               uint64_t data,
                               int fd);
/**
 * memory_region_add_subregion: Add a sub-region to a container.
 *
 * Adds a sub-region at @offset.  The sub-region may not overlap with other
 * subregions (except for those explicitly marked as overlapping).  A region
 * may only be added once as a subregion (unless removed with
 * memory_region_del_subregion()); use memory_region_init_alias() if you
 * want a region to be a subregion in multiple locations.
 *
 * @mr: the region to contain the new subregion; must be a container
 *      initialized with memory_region_init().
 * @offset: the offset relative to @mr where @subregion is added.
 * @subregion: the subregion to be added.
 */
void memory_region_add_subregion(MemoryRegion *mr,
                                 target_phys_addr_t offset,
                                 MemoryRegion *subregion);
/**
 * memory_region_add_subregion: Add a sub-region to a container, with overlap.
 *
 * Adds a sub-region at @offset.  The sub-region may overlap with other
 * subregions.  Conflicts are resolved by having a higher @priority hide a
 * lower @priority. Subregions without priority are taken as @priority 0.
 * A region may only be added once as a subregion (unless removed with
 * memory_region_del_subregion()); use memory_region_init_alias() if you
 * want a region to be a subregion in multiple locations.
 *
 * @mr: the region to contain the new subregion; must be a container
 *      initialized with memory_region_init().
 * @offset: the offset relative to @mr where @subregion is added.
 * @subregion: the subregion to be added.
 * @priority: used for resolving overlaps; highest priority wins.
 */
void memory_region_add_subregion_overlap(MemoryRegion *mr,
                                         target_phys_addr_t offset,
                                         MemoryRegion *subregion,
                                         unsigned priority);
/**
 * memory_region_del_subregion: Remove a subregion.
 *
 * Removes a subregion from its container.
 *
 * @mr: the container to be updated.
 * @subregion: the region being removed; must be a current subregion of @mr.
 */
void memory_region_del_subregion(MemoryRegion *mr,
                                 MemoryRegion *subregion);

/* Start a transaction; changes will be accumulated and made visible only
 * when the transaction ends.
 */
void memory_region_transaction_begin(void);
/* Commit a transaction and make changes visible to the guest.
 */
void memory_region_transaction_commit(void);

void mtree_info(fprintf_function mon_printf, void *f);

#endif

#endif
