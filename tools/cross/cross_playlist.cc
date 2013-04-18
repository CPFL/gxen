/*
 * Cross FIFO queue playlist
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
#include "cross.h"
#include "cross_page.h"
#include "cross_playlist.h"
#include "cross_context.h"
#include "cross_pramin.h"
namespace cross {

playlist::playlist()
    : pages_(new page[2])
    , cursor_(0)
{
}

uint64_t playlist::update(context* ctx, uint64_t address, uint32_t count) {
    // scan fifo and update values
    page* page = toggle();
    pramin::accessor pramin;
    uint32_t i;
    CROSS_LOG("FIFO playlist update %u\n", count);
    for (i = 0; i < count; ++i) {
        const uint32_t cid = pramin.read32(address + i * 0x8);
        CROSS_LOG("FIFO playlist cid %u => %u\n", cid, ctx->get_phys_channel_id(cid));
        page->write32(i * 0x8 + 0x0, ctx->get_phys_channel_id(cid));
        page->write32(i * 0x8 + 0x4, 0x4);
    }
    return page->address();
}

page* playlist::toggle() {
    cursor_ ^= 1;
    return &pages_[cursor_ & 0x1];
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
