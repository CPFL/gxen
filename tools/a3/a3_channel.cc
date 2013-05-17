/*
 * Cross Channel
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
#include <utility>
#include "a3.h"
#include "a3_context.h"
#include "a3_channel.h"
#include "a3_shadow_page_table.h"
#include "a3_barrier.h"
#include "a3_pramin.h"
#include "a3_inttypes.h"
#include "a3_page.h"
#include "a3_bit_mask.h"
namespace a3 {

template<typename T>
static uint64_t read64(T* pramin, uint64_t addr) {
    const uint32_t lower = pramin->read32(addr);
    const uint32_t upper = pramin->read32(addr + 0x4);
    return lower | (static_cast<uint64_t>(upper) << 32);
}

template<typename T>
static void write64(T* pramin, uint64_t addr, uint64_t value) {
    pramin->write32(addr, bit_mask<32>(value));
    pramin->write32(addr + 0x4, value >> 32);
}

channel::channel(int id)
    : id_(id)
    , enabled_(false)
    , ramin_address_()
    , table_(new shadow_page_table(id))
    , shadow_ramin_(new page(1)) {
}

channel::~channel() {
}

void channel::detach(context* ctx, uint64_t addr) {
    A3_LOG("detach from 0x%" PRIX64 " to 0x%" PRIX64 "\n", ramin_address(), addr);
    ctx->barrier()->unmap(ramin_address());

    typedef context::channel_map::iterator iter_t;
    const std::pair<iter_t, iter_t> range = ctx->ramin_channel_map()->equal_range(addr);
    for (iter_t it = range.first; it != range.second; ++it) {
        if (it->second == this) {
            ctx->ramin_channel_map()->erase(it);
            break;
        }
    }
}

void channel::shadow(context* ctx) {
    uint64_t page_directory_virt = 0;
    uint64_t page_directory_phys = 0;
    uint64_t page_directory_size = 0;

    pramin::accessor pramin;

    // shadow ramin
    for (uint64_t offset = 0; offset < 0x1000; offset += 0x4) {
        const uint32_t value = pramin.read32(ramin_address() + offset);
        shadow_ramin()->write32(offset, value);
    }

    // and adjust address
    // page directory
    page_directory_virt = read64(&pramin, ramin_address() + 0x0200);
    page_directory_phys = ctx->get_phys_address(page_directory_virt);
    page_directory_size = read64(&pramin, ramin_address() + 0x0208);

    write64(shadow_ramin(), 0x0208, page_directory_size);
    A3_LOG("virt 0x%" PRIX64 " phys 0x%" PRIX64 "\n", page_directory_virt, page_directory_phys);

    // fctx
    const uint64_t fctx_virt = read64(&pramin, ramin_address() + 0x08);
    const uint64_t fctx_phys = ctx->get_phys_address(fctx_virt);
    write64(shadow_ramin(), 0x08, fctx_phys);

    // mpeg ctx
    const uint64_t mpeg_ctx_limit_virt = pramin.read32(ramin_address() + 0x60 + 0x04);
    const uint64_t mpeg_ctx_limit_phys = ctx->get_phys_address(mpeg_ctx_limit_virt);
    shadow_ramin()->write32(0x60 + 0x04, mpeg_ctx_limit_phys);

    const uint64_t mpeg_ctx_virt = pramin.read32(ramin_address() + 0x60 + 0x08);
    const uint64_t mpeg_ctx_phys = ctx->get_phys_address(mpeg_ctx_virt);
    shadow_ramin()->write32(0x60 + 0x08, mpeg_ctx_phys);

    if (table()->refresh(ctx, page_directory_phys, page_directory_size)) {
        write64(shadow_ramin(), 0x0200, table()->shadow_address());
    }
}

void channel::attach(context* ctx, uint64_t addr) {
    shadow(ctx);
    ctx->ramin_channel_map()->insert(std::make_pair(ramin_address(), this));
    ctx->barrier()->map(ramin_address());
}

uint64_t channel::refresh(context* ctx, uint64_t addr) {
    A3_LOG("mapping 0x%" PRIX64 " with shadow 0x%" PRIX64 "\n", addr, shadow_ramin()->address());
    if (enabled()) {
        if (addr == ramin_address()) {
            // same channel ramin
            return shadow_ramin()->address();
        }
        detach(ctx, addr);
    }
    enabled_ = true;
    ramin_address_ = addr;
    attach(ctx, addr);
    return shadow_ramin()->address();
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
