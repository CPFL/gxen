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
#include "make_unique.h"
namespace a3 {

void flush_bar() {
    // flush
    registers::accessor regs;
    regs.write32(0x070000, 0x00000001);
    if (!regs.wait_eq(0x070000, 0x00000002, 0x00000000)) {
        A3_LOG("flush timeout\n");
    }
}

template<typename engine_t>
void playlist_update(context* ctx, engine_t* engine, uint64_t address, uint32_t cmd, uint32_t status) {
    // scan fifo and update values
    pmem::accessor pmem;

    // at first, clear ctx channel enables
    for (uint32_t i = 0; i < A3_DOMAIN_CHANNELS; ++i) {
        const uint32_t cid = ctx->get_phys_channel_id(i);
        // A3_LOG("1: playlist update id %u\n", cid);
        engine->set(cid, false);
    }

    const uint32_t count = bit_mask<8, uint32_t>(cmd);
    A3_LOG("playlist update %u\n", count);

    if (!count) {
        return;
    }

    page* page = engine->toggle();

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t vid = pmem.read32(address + i * 0x8);
        const uint32_t cid = ctx->get_phys_channel_id(vid);
        A3_LOG("2: playlist update id %u => %u / %u\n", i, cid, vid);
        engine->set(cid, true);
    }

    uint32_t phys_count = 0;
    for (uint32_t i = 0; i < A3_CHANNELS; ++i) {
        if (engine->get(i)) {
            page->write32(phys_count * 0x8 + 0x0, i);
            page->write32(phys_count * 0x8 + 0x4, status);
            ++phys_count;
        }
    }

    const uint64_t shadow = page->address();
    const uint32_t phys_cmd = ((cmd >> 20) << 20) | phys_count;
    registers::accessor regs;
    A3_LOG("playlist address shadow %x provided %x\n", static_cast<unsigned>(shadow >> 12), static_cast<unsigned>(address >> 12));
    regs.write32(0x2270, shadow >> 12);
    regs.write32(0x2274, phys_cmd);
    A3_LOG("playlist cmd from %x to %x\n", cmd, phys_cmd);
}

void nvc0_playlist_t::update(context* ctx, uint64_t address, uint32_t cmd) {
    playlist_update(ctx, &engine_, address, cmd, 0x4);
}

void nve0_playlist_t::update(context* ctx, uint64_t address, uint32_t cmd) {
    const uint32_t eng = (cmd >> 20);
    ASSERT(eng < engines_.size());
    A3_LOG("playlist engine %u\n",eng);
    playlist_update(ctx, &engines_[eng], address, cmd, 0x0);
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
