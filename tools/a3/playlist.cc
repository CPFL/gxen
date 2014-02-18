/*
 * A3 FIFO queue playlist
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
#include <cstdio>
#include "a3.h"
#include "page.h"
#include "playlist.h"
#include "context.h"
#include "pmem.h"
#include "bit_mask.h"
#include "registers.h"
namespace a3 {

playlist_t::playlist_t()
    : pages_(new page[2])
    , channels_()
    , cursor_(0)
{
}

void playlist_t::update(context* ctx, uint64_t address, uint32_t cmd) {
    // scan fifo and update values
    pmem::accessor pmem;

    // at first, clear ctx channel enables
    for (uint32_t i = 0; i < A3_DOMAIN_CHANNELS; ++i) {
        const uint32_t cid = ctx->get_phys_channel_id(i);
        // A3_LOG("1: playlist update id %u\n", cid);
        channels_.set(cid, 0);
    }

    const uint32_t count = bit_mask<8, uint32_t>(cmd);
    A3_LOG("playlist update %u\n", count);

    if (!count) {
        return;
    }

    page* page = toggle();

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t vid = pmem.read32(address + i * 0x8);
        const uint32_t cid = ctx->get_phys_channel_id(vid);
        A3_LOG("2: playlist update id %u => %u / %u\n", i, cid, vid);
        channels_.set(cid, 1);
    }

    uint32_t phys_count = 0;
    for (uint32_t i = 0; i < A3_CHANNELS; ++i) {
        if (channels_[i]) {
            A3_LOG("playlist update id %u\n", i);
            page->write32(phys_count * 0x8 + 0x0, i);
            page->write32(phys_count * 0x8 + 0x4, 0x4);
            ++phys_count;
        }
    }

    const uint64_t shadow = page->address();
    const uint32_t phys_cmd = bit_clear<8, uint32_t>(cmd) | phys_count;
    registers::accessor regs;
    regs.write32(0x2270, shadow >> 12);
    regs.write32(0x2274, phys_cmd);

    // if (!regs.wait_eq(0x00227c, 0x00100000, 0x00000000)) {
    //     A3_LOG("playlist update failed\n");
    // }
    A3_LOG("playlist cmd from %u to %u\n", cmd, phys_cmd);
}

page* playlist_t::toggle() {
    cursor_ ^= 1;
    return &pages_[cursor_ & 0x1];
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
