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
#include <stdint.h>
#include "a3_device.h"
#include "a3_device_bar3.h"
#include "a3_context.h"
#include "a3_bit_mask.h"
#include "a3_size.h"
#include "a3_page_table.h"
#include "a3_software_page_table.h"
#include "a3_barrier.h"
#include "a3_mmio.h"
namespace a3 {

// TODO(Yusuke Suzuki):
// This is hard coded value 16MB
static const uint64_t kAreaSize = 16 * size::MB;

device_bar3::device_bar3(device::bar_t bar)
    : address_(bar.base_addr)
    , size_(bar.size)
    , ramin_(2)
    , directory_(8)
    , entries_(32)
    , xen_(bar.size / kPAGE_SIZE, false)
{
    const uint64_t vm_size = size() - 1;
    ramin_.clear();
    directory_.clear();
    entries_.clear();

    // construct channel ramin
    mmio::write64(&ramin_, 0x0200, directory_.address());
    mmio::write64(&ramin_, 0x0208, vm_size);

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
    const uint64_t host = address() + ctx->id() * kAreaSize + offset;
    A3_LOG("mapping %" PRIx64 " to %" PRIx64 "\n", guest, host);
    a3_xen_add_memory_mapping(device::instance()->xl_ctx(), ctx->domid(), guest >> kPAGE_SHIFT, host >> kPAGE_SHIFT, 1);
}

void device_bar3::unmap_xen_page(context* ctx, uint64_t offset) {
    const uint64_t guest = ctx->bar3_address() + offset;
    const uint64_t host = address() + ctx->id() * kAreaSize + offset;
    A3_LOG("unmapping %" PRIx64 " to %" PRIx64 "\n", guest, host);
    a3_xen_remove_memory_mapping(device::instance()->xl_ctx(), ctx->domid(), guest >> kPAGE_SHIFT, host >> kPAGE_SHIFT, 1);
}

void device_bar3::map(uint64_t index, const struct page_entry& entry) {
    entries_.write32(0x8 * index, entry.word0);
    entries_.write32(0x8 * index + 0x4, entry.word1);
}

void device_bar3::shadow(context* ctx, uint64_t phys) {
    A3_LOG("%" PRIu32 " BAR3 shadowed\n", ctx->id());
    // At first map all
    a3_xen_add_memory_mapping(device::instance()->xl_ctx(), ctx->domid(), ctx->bar3_address() >> kPAGE_SHIFT, (address() + ctx->id() * kAreaSize) >> kPAGE_SHIFT, kAreaSize / 0x1000);
    for (uint64_t address = 0; address < kAreaSize; address += kPAGE_SIZE) {
        const uint64_t virt = ctx->id() * kAreaSize + address;
        struct software_page_entry entry;
        const uint64_t gphys = ctx->bar3_channel()->table()->resolve(address, &entry);
        const uint64_t index = virt / kPAGE_SIZE;
        if (gphys != UINT64_MAX) {
            // check this is not ramin
            barrier::page_entry* barrier_entry = NULL;
            if (!ctx->barrier()->lookup(gphys, &barrier_entry, false)) {
                // entry is found
                map(index, entry.phys());
            } else {
                unmap_xen_page(ctx, address);
            }
        } else {
            const struct page_entry entry = { };
            map(index, entry);
        }
    }
}

void device_bar3::flush() {
    A3_SYNCHRONIZED(device::instance()->mutex_handle()) {
        const uint32_t engine = 1 | 4;
        registers::accessor registers;
        registers.write32(0x100cb8, directory_.address() >> 8);
        registers.write32(0x100cbc, 0x80000000 | engine);
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
