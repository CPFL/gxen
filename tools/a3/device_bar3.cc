/*
 * A3 device BAR3
 *
 * Copyright (c) 2012-2013 Yusuke Suzuki
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
#include <cstdint>
#include <boost/logic/tribool.hpp>
#include "device.h"
#include "device_bar3.h"
#include "context.h"
#include "bit_mask.h"
#include "size.h"
#include "page_table.h"
#include "software_page_table.h"
#include "barrier.h"
#include "mmio.h"
#include "page_table.h"
#include "registers.h"
namespace a3 {

device_bar3::device_bar3(device_t::bar_t bar)
    : address_(bar.base_addr)
    , size_(bar.size)
    , ramin_(1)
    , directory_(8)
    , entries_(A3_BAR3_TOTAL_SIZE / 0x1000 / 0x1000 * 8)
    , software_(A3_BAR3_TOTAL_SIZE / 0x8)
    , large_()
    , small_()
{
    ramin_.clear();
    directory_.clear();
    entries_.clear();

    // construct channel ramin
    mmio::write64(&ramin_, 0x0200, directory_.address());
    mmio::write64(&ramin_, 0x0208, A3_BAR3_TOTAL_SIZE - 1);

    // construct minimum page table
    struct page_directory dir = { };
    dir.word0 = 0;
    dir.word1 = (entries_.address()) >> 8 | 0x1;
    directory_.write32(0x0, dir.word0);
    directory_.write32(0x4, dir.word1);
    refresh();
}

void device_bar3::refresh() {
    // set ramin as BAR3 channel
    registers::write32(0x001714, 0xc0000000 | ramin_.address() >> 12);
}

void device_bar3::map_xen_page(context* ctx, uint64_t offset) {
    const uint64_t guest = ctx->bar3_address() + offset;
    const uint64_t host = address() + ctx->id() * A3_BAR3_ARENA_SIZE + offset;
    // A3_LOG("mapping %" PRIx64 " to %" PRIx64 "\n", guest, host);
    if (a3::flags::bar3_remapping) {
        a3_xen_add_memory_mapping(device()->xl_ctx(), ctx->domid(), guest >> kPAGE_SHIFT, host >> kPAGE_SHIFT, 1);
    }
}

void device_bar3::unmap_xen_page(context* ctx, uint64_t offset) {
    const uint64_t guest = ctx->bar3_address() + offset;
    const uint64_t host = address() + ctx->id() * A3_BAR3_ARENA_SIZE + offset;
    // A3_LOG("unmapping %" PRIx64 " to %" PRIx64 "\n", guest, host);
    if (a3::flags::bar3_remapping) {
        a3_xen_remove_memory_mapping(device()->xl_ctx(), ctx->domid(), guest >> kPAGE_SHIFT, host >> kPAGE_SHIFT, 1);
    }
}

void device_bar3::map_xen_page_batch(context* ctx, uint64_t offset, uint32_t count) {
    const uint64_t guest = ctx->bar3_address() + offset;
    const uint64_t host = address() + ctx->id() * A3_BAR3_ARENA_SIZE + offset;
    A3_LOG("batch mapping %" PRIx64 " to %" PRIx64 " %" PRIu32 "\n", guest, host, count);
    if (a3::flags::bar3_remapping) {
        a3_xen_add_memory_mapping(device()->xl_ctx(), ctx->domid(), guest >> kPAGE_SHIFT, host >> kPAGE_SHIFT, count);
    }
}

void device_bar3::unmap_xen_page_batch(context* ctx, uint64_t offset, uint32_t count) {
    const uint64_t guest = ctx->bar3_address() + offset;
    const uint64_t host = address() + ctx->id() * A3_BAR3_ARENA_SIZE + offset;
    A3_LOG("batch unmapping %" PRIx64 " to %" PRIx64 " %" PRIu32 "\n", guest, host, count);
    if (a3::flags::bar3_remapping) {
        a3_xen_remove_memory_mapping(device()->xl_ctx(), ctx->domid(), guest >> kPAGE_SHIFT, host >> kPAGE_SHIFT, count);
    }
}

void device_bar3::map(uint64_t index, const struct page_entry& entry) {
    entries_.write32(0x8 * index, entry.word0);
    entries_.write32(0x8 * index + 0x4, entry.word1);
    const uint64_t addr = static_cast<uint64_t>(entry.address) << 12;
    software_[index] = (entry.present) ? addr : 0ULL;
}

void device_bar3::shadow(context* ctx, uint64_t phys) {
    A3_LOG("%" PRIu32 " BAR3 shadowed\n", ctx->id());
    // At first remove all
    unmap_xen_page_batch(ctx, 0, A3_BAR3_ARENA_SIZE / 0x1000);

    // FIXME(Yusuke Suzuki): optimize it
    for (uint64_t address = 0; address < A3_BAR3_ARENA_SIZE; address += kPAGE_SIZE) {
        const uint64_t virt = ctx->id() * A3_BAR3_ARENA_SIZE + address;
        struct software_page_entry entry;
        const uint64_t gphys = resolve(ctx, address, &entry);
        const uint64_t index = virt / kPAGE_SIZE;
        if (gphys != UINT64_MAX) {
            // check this is not ramin
            barrier::page_entry* barrier_entry = nullptr;
            map(index, entry.phys());
            if (ctx->barrier()->lookup(gphys, &barrier_entry, false)) {
                // unmap_xen_page(ctx, address);
            } else {
                map_xen_page(ctx, address);
            }
        } else {
            const struct page_entry entry = { };
            map(index, entry);
            // unmap_xen_page(ctx, address);
        }
    }
}

void device_bar3::reset_barrier(context* ctx, uint64_t old, uint64_t addr, bool old_remap) {
    const uint64_t shift = ctx->id() * A3_BAR3_ARENA_SIZE / kPAGE_SIZE;
    for (uint64_t index = 0, iz = A3_BAR3_ARENA_SIZE / kPAGE_SIZE; index < iz; ++index) {
        const uint64_t hindex = shift + index;
        const uint64_t target = software_[hindex];
        if (target == old && old_remap) {
            map_xen_page(ctx, index * kPAGE_SIZE);
        } else if (target == addr) {
            unmap_xen_page(ctx, index * kPAGE_SIZE);
        }
    }
}

void device_bar3::flush() {
    A3_SYNCHRONIZED(device()->mutex()) {
        const uint32_t engine = 1 | 4;
        registers::accessor registers;
        registers.write32(0x100cb8, directory_.address() >> 8);
        registers.write32(0x100cbc, 0x80000000 | engine);
    }
}

uint64_t device_bar3::resolve(context* ctx, uint64_t gvaddr, struct software_page_entry* result) {
    const uint32_t dir = gvaddr / kPAGE_DIRECTORY_COVERED_SIZE;
    if (dir != 0) {
        return UINT64_MAX;
    }

    const uint64_t hvaddr = gvaddr + ctx->id() * A3_BAR3_ARENA_SIZE;
    {
        const uint64_t index = hvaddr / kSMALL_PAGE_SIZE;
        const uint64_t rest = hvaddr % kSMALL_PAGE_SIZE;
        if (small_.size() > index) {
            const struct software_page_entry& entry = small_[index];
            if (entry.present()) {
                if (result) {
                    *result = entry;
                }
                const uint64_t address = entry.phys().address;
                return (address << 12) + rest;
            }
        }
    }

    {
        const uint64_t index = hvaddr / kLARGE_PAGE_SIZE;
        const uint64_t rest = hvaddr % kLARGE_PAGE_SIZE;
        if (large_.size() > index) {
            const struct software_page_entry& entry = large_[index];
            if (entry.present()) {
                if (result) {
                    *result = entry;
                }
                const uint64_t address = entry.phys().address;
                return (address << 12) + rest;
            }
        }
    }

    return UINT64_MAX;
}

void device_bar3::pv_reflect(context* ctx, uint32_t index, uint64_t guest, uint64_t host) {
    // software page table
    const uint64_t hindex = index + ((ctx->id() * A3_BAR3_ARENA_SIZE) / kPAGE_SIZE);
    const uint64_t goffset = (index * kPAGE_SIZE);
    struct page_entry entry;

    entry.raw = guest;
    small_[hindex].refresh(ctx, entry);

    entry.raw = host;

    if (host) {
        // check this is not ramin
        barrier::page_entry* barrier_entry = nullptr;
        const uint64_t gphys = static_cast<uint64_t>(entry.address) << 12;
        map(hindex, entry);
        if (!ctx->barrier()->lookup(gphys, &barrier_entry, false)) {
            map_xen_page(ctx, goffset);
        } else {
            unmap_xen_page(ctx, goffset);
        }
    } else {
        map(hindex, entry);
        unmap_xen_page(ctx, goffset);
    }
}

void device_bar3::pv_reflect_batch(context* ctx, uint32_t index, uint64_t guest, uint64_t next, uint32_t count) {
    // mode true          => map
    //      false         => unmap
    //      indeterminate => init
    boost::logic::tribool mode = boost::logic::indeterminate;
    int32_t range = -1;
    uint64_t init_page = -1;
    for (uint32_t i = 0; i < count; ++i, guest += next) {
        const uint64_t hindex = index + i + ((ctx->id() * A3_BAR3_ARENA_SIZE) / kPAGE_SIZE);
        const uint64_t goffset = ((index + i) * kPAGE_SIZE);
        struct page_entry gentry;
        gentry.raw = guest;
        small_[hindex].refresh(ctx, gentry);
        const struct page_entry entry = ctx->guest_to_host(gentry);
        map(hindex, entry);
        if (entry.raw) {
            barrier::page_entry* barrier_entry = nullptr;
            const uint64_t gphys = static_cast<uint64_t>(entry.address) << 12;
            if (!ctx->barrier()->lookup(gphys, &barrier_entry, false)) {
                // map
                if (mode) {
                    // map continues, increment range
                    ++range;
                    continue;
                }

                if (!mode) {
                    // flush unmapping
                    unmap_xen_page_batch(ctx, init_page, range);
                }
                mode = true;
                init_page = goffset;
                range = 1;
                continue;
            }
        }

        // unmap
        if (!mode) {
            ++range;
            continue;
        }

        if (mode) {
            map_xen_page_batch(ctx, init_page, range);
        }
        mode = false;
        init_page = goffset;
        range = 1;
    }

    // flush
    if (mode) {
        map_xen_page_batch(ctx, init_page, range);
    } else if (!mode) {
        unmap_xen_page_batch(ctx, init_page, range);
    }
}

void device_bar3::refresh_table(context* ctx, uint64_t phys) {
    pmem::accessor pmem;
    if (!phys) {
        return;
    }

    // TODO(Yusuke Suzuki): validation needed
    struct page_directory dir = page_directory::create(&pmem, phys);
    if (dir.large_page_table_present) {
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.large_page_table_address) << 12);
        const std::size_t count = std::min<std::size_t>(A3_BAR3_ARENA_SIZE/ kLARGE_PAGE_SIZE, page_directory::large_size_count(dir));
        ASSERT(count <= kLARGE_PAGE_COUNT);
        for (std::size_t i = 0; i < count; ++i) {
            const uint64_t hindex = i + ((ctx->id() * A3_BAR3_ARENA_SIZE) / kLARGE_PAGE_SIZE);
            const uint64_t item = 0x8 * i;
            struct page_entry entry;
            if (page_entry::create(&pmem, address + item, &entry)) {
                large_[hindex].refresh(ctx, entry);
            } else {
                large_[hindex].clear();
            }
        }
    } else {
        struct software_page_entry entry = { };
        std::fill(large_.begin() + ((ctx->id() * A3_BAR3_ARENA_SIZE) / kLARGE_PAGE_SIZE),
                  large_.begin() + (((ctx->id() + 1)* A3_BAR3_ARENA_SIZE) / kLARGE_PAGE_SIZE), entry);
    }

    if (dir.small_page_table_present) {
        const uint64_t address = ctx->get_phys_address(static_cast<uint64_t>(dir.small_page_table_address) << 12);
        const std::size_t count = A3_BAR3_ARENA_SIZE / kSMALL_PAGE_SIZE;
        ASSERT(count <= kSMALL_PAGE_COUNT);
        for (std::size_t i = 0; i < count; ++i) {
            const uint64_t hindex = i + ((ctx->id() * A3_BAR3_ARENA_SIZE) / kSMALL_PAGE_SIZE);
            const uint64_t item = 0x8 * i;
            struct page_entry entry;
            if (page_entry::create(&pmem, address + item, &entry)) {
                small_[hindex].refresh(ctx, entry);
            } else {
                small_[hindex].clear();
            }
        }
    } else {
        struct software_page_entry entry = { };
        std::fill(small_.begin() + ((ctx->id() * A3_BAR3_ARENA_SIZE) / kSMALL_PAGE_SIZE),
                  small_.begin() + (((ctx->id() + 1)* A3_BAR3_ARENA_SIZE) / kSMALL_PAGE_SIZE), entry);
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
