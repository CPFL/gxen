/*
 * A3 device BAR1
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
#include <cinttypes>
#include "bit_mask.h"
#include "device_table.h"
#include "pmem.h"
#include "shadow_page_table.h"
#include "software_page_table.h"
#include "device_bar1.h"
#include "context.h"
#include "mmio.h"
#include "registers.h"
namespace a3 {

device_bar1::device_bar1(device_t::bar_t bar)
    : ramin_(1)
    , directory_(8)
    , entry_()
    , range_(device()->chipset()->type() == card::NVC0 ? 0x001000 : 0x000200)
    {
    const uint64_t vm_size = (range_ * 128) - 1;
    ramin_.clear();
    directory_.clear();

    // construct channel ramin
    mmio::write64(&ramin_, 0x0200, directory_.address());
	ramin_.write32(0x0208, 0xffffffff);
	ramin_.write32(0x020c, 0x000000ff);

    // construct minimum page table
    struct page_directory dir = { };
    dir.word0 = 0;
    dir.word1 = (entry_.address()) >> 8 | 0x1;
    directory_.write32(0x0, dir.word0);
    directory_.write32(0x4, dir.word1);

    // refresh_channel();
    refresh_poll_area();
    refresh();

    A3_LOG("construct shadow BAR1 channel %" PRIX64 " with PDE %" PRIX64 " PTE %" PRIX64 " \n", ramin_.address(), directory_.address(), entry_.address());
}

void device_bar1::refresh() {
    // set ramin as BAR1 channel
    registers::write32(0x001704, 0x80000000 | ramin_.address() >> 12);
}

void device_bar1::refresh_poll_area() {
    // set 0 as POLL_AREA
    registers::accessor registers;
    if (device()->chipset()->type() == card::NVC0) {
        registers.mask32(0x002200, 0x00000001, 0x00000001);
    }
    registers.write32(0x2254, 0x10000000 | 0x0);
}

void device_bar1::shadow(context* ctx) {
    A3_LOG("%" PRIu32 " BAR1 shadowed\n", ctx->id());
    for (uint32_t vcid = 0; vcid < A3_DOMAIN_CHANNELS; ++vcid) {
        const uint64_t offset = vcid * range_ + ctx->poll_area()->area();
        const uint32_t pcid = ctx->get_phys_channel_id(vcid);
        const uint64_t virt = pcid * range_;
        struct software_page_entry entry;
        const uint64_t gphys = ctx->bar1_channel()->table()->resolve(offset, &entry);
        if (gphys != UINT64_MAX) {
            map(virt, entry.phys());
        }
    }
}

void device_bar1::map(uint64_t virt, const struct page_entry& entry) {
    if ((virt / kPAGE_DIRECTORY_COVERED_SIZE) != 0) {
        return;
    }
    const uint64_t index = virt / kSMALL_PAGE_SIZE;
    // ASSERT((virt % kSMALL_PAGE_SIZE) == 0);
    entry_.write32(0x8 * index, entry.word0);
    entry_.write32(0x8 * index + 0x4, entry.word1);
    A3_LOG("  BAR1 table %" PRIX64 " mapped to %" PRIX64 "\n", virt, entry.raw);
}

void device_bar1::flush() {
    A3_SYNCHRONIZED(device()->mutex()) {
        const uint32_t engine = 1 | 4;
        registers::accessor registers;
        registers.wait_ne(0x100c80, 0x00ff0000, 0x00000000);
        registers.write32(0x100cb8, directory_.address() >> 8);
        registers.write32(0x100cbc, 0x80000000 | engine);
        registers.wait_eq(0x100c80, 0x00008000, 0x00008000);
    }
}

void device_bar1::write(context* ctx, const command& cmd) {
    uint64_t offset = cmd.offset - ctx->poll_area()->area();
    offset += range_ * ctx->id() * A3_DOMAIN_CHANNELS;
    device()->write(1, offset, cmd.value, cmd.size());
}

uint32_t device_bar1::read(context* ctx, const command& cmd) {
    uint64_t offset = cmd.offset - ctx->poll_area()->area();
    offset += range_ * ctx->id() * A3_DOMAIN_CHANNELS;
    return device()->read(1, offset, cmd.size());
}

void device_bar1::pv_scan(context* ctx) {
    A3_LOG("%" PRIu32 " BAR1 shadowed\n", ctx->id());
    for (uint32_t vcid = 0; vcid < A3_DOMAIN_CHANNELS; ++vcid) {
        const uint64_t offset = vcid * range_ + ctx->poll_area()->area();
        const uint32_t pcid = ctx->get_phys_channel_id(vcid);
        const uint64_t virt = pcid * range_;
        struct software_page_entry entry;
        const uint64_t gphys = ctx->bar1_channel()->table()->resolve(offset, &entry);
        if (gphys != UINT64_MAX) {
            map(virt, entry.phys());
        }
    }
}

void device_bar1::pv_reflect_entry(context* ctx, bool big, uint32_t index, uint64_t host) {
    A3_LOG("%" PRIu32 " BAR1 reflect entry %" PRIx32 "\n", ctx->id(), index);
    struct page_entry entry;
    entry.raw = host;
    if (big) {
    } else {
        map(((ctx->id() * A3_DOMAIN_CHANNELS) + index) * range_, entry);
    }
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
