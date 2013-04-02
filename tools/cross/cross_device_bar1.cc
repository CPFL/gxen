/*
 * Cross device table
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
#include "cross_inttypes.h"
#include "cross_bit_mask.h"
#include "cross_device_table.h"
#include "cross_pramin.h"
#include "cross_shadow_page_table.h"
#include "cross_device_bar1.h"
#include "cross_context.h"
namespace cross {

static uint64_t encode_address(uint64_t phys) {
    phys >>= 8;
    phys |= 0x00000001; /* present */
    return phys;
}

static uint32_t lower32(uint64_t data) {
    return bit_mask<32>(data);
}

static uint32_t upper32(uint64_t data) {
    return bit_mask<32>(data >> 32);
}

device_bar1::device_bar1()
    : ramin_(2)
    , directory_()
    , entry_() {
    const uint64_t vm_size = 0x1000 * 128;
    // construct channel ramin
    ramin_.write32(0x0200, lower32(directory_.address()));
    ramin_.write32(0x0204, lower32(directory_.address()));
	ramin_.write32(0x0208, lower32(vm_size));
	ramin_.write32(0x020c, upper32(vm_size));

    // construct minimum page table
    struct page_directory dir = { };
    dir.word0 = 0;
    dir.small_page_table_present = 1;
    dir.unknown1 = 0;
    dir.unknown2 = 0;
    dir.unknown3 = 0;
    dir.small_page_table_address = (entry_.address()) >> 12;
    directory_.write32(0x0, dir.word0);
    directory_.write32(0x4, dir.word1);

    CROSS_LOG("construct shadow BAR1 channel %" PRIX64 " with PDE %" PRIX64 " PTE %" PRIX64 " \n", ramin_.address(), directory_.address(), entry_.address());
}

void device_bar1::shadow(context* ctx) {
    CROSS_LOG("%" PRIu32 " BAR1 shadowed\n", ctx->id());
    for (uint32_t vcid = 0; vcid < CROSS_DOMAIN_CHANNELS; ++vcid) {
        // TODO(Yusuke Suzuki): remove this shift get_phys_channel_id
        const uint64_t offset = (ctx->get_phys_channel_id(vcid) * 0x1000ULL) + ctx->poll_area();
        const uint64_t gphys = ctx->bar1_channel()->table()->resolve(offset);
        if (gphys != UINT64_MAX) {
            const uint32_t pcid = ctx->get_phys_channel_id(vcid);
            const uint64_t virt = pcid * 0x1000ULL;
            map(virt, gphys);
        }
    }
}

void device_bar1::map(uint64_t virt, uint64_t phys) {
    if ((virt / kPAGE_DIRECTORY_COVERED_SIZE) != 0) {
        return;
    }
    const uint64_t index = virt / kSMALL_PAGE_SIZE;
    assert((virt % kSMALL_PAGE_SIZE) == 0);
    const uint64_t data = encode_address(phys);
    entry_.write32(0x8 * index, lower32(data));
    entry_.write32(0x8 * index + 0x4, upper32(data));
    CROSS_LOG("  BAR1 table %" PRIX64 " mapped to %" PRIX64 "\n", virt, phys);
}

void device_bar1::flush() {
    CROSS_SYNCHRONIZED(device::instance()->mutex_handle()) {
        const uint32_t engine = 1 | 4;
        registers::accessor registers;
        registers.wait_ne(0x100c80, 0x00ff0000, 0x00000000);
        registers.write32(0x100cb8, directory_.address() >> 8);
        registers.write32(0x100cbc, engine);
        registers.wait_eq(0x100c80, 0x00008000, 0x00008000);
    }
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
