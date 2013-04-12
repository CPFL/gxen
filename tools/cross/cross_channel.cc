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
#include "cross.h"
#include "cross_context.h"
#include "cross_channel.h"
#include "cross_shadow_page_table.h"
#include "cross_barrier.h"
#include "cross_pramin.h"
#include "cross_inttypes.h"
#include "cross_page.h"
#include "cross_bit_mask.h"
namespace cross {

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
    , ramin_(new page(2)) {
}

channel::~channel() {
}

void channel::detach(context* ctx, uint64_t addr) {
    CROSS_LOG("detach from 0x%" PRIX64 " to 0x%" PRIX64 "\n", ramin_address(), addr);
    ctx->barrier()->unmap(ramin_address());
    {
        pramin::accessor pramin;

        // page directory
        const uint64_t page_directory_phys = read64(&pramin, ramin_address() + 0x0200);
        const uint64_t page_directory_virt = ctx->get_virt_address(page_directory_phys);
        write64(&pramin, ramin_address() + 0x0200, page_directory_virt);
        CROSS_LOG("virt 0x%" PRIX64 " phys 0x%" PRIX64 "\n", page_directory_virt, page_directory_phys);

        // fctx
        const uint64_t fctx_phys = read64(&pramin, ramin_address() + 0x08);
        const uint64_t fctx_virt = ctx->get_virt_address(fctx_phys);
        write64(&pramin, ramin_address() + 0x08, fctx_virt);

        const uint64_t mpeg_ctx_limit_phys = pramin.read32(ramin_address() + 0x60 + 0x04);
        const uint64_t mpeg_ctx_limit_virt = ctx->get_virt_address(mpeg_ctx_limit_phys);
        pramin.write32(ramin_address() + 0x60 + 0x04, mpeg_ctx_limit_virt);
        const uint64_t mpeg_ctx_phys = pramin.read32(ramin_address() + 0x60 + 0x08);
        const uint64_t mpeg_ctx_virt = ctx->get_virt_address(mpeg_ctx_phys);
        pramin.write32(ramin_address() + 0x60 + 0x08, mpeg_ctx_virt);
    }
}

void channel::attach(context* ctx, uint64_t addr) {
    uint64_t page_directory_virt = 0;
    uint64_t page_directory_phys = 0;
    uint64_t page_directory_size = 0;
    {
        pramin::accessor pramin;

        // shadow ramin
        for (uint64_t offset = 0; offset < ramin_->size(); offset += 0x4) {
            const uint32_t value = pramin.read32(ramin_address() + offset);
            ramin_->write32(offset, value);
        }

        // and adjust address
        // page directory
        page_directory_virt = read64(&pramin, ramin_address() + 0x0200);
        page_directory_phys = ctx->get_phys_address(page_directory_virt);
        page_directory_size = read64(&pramin, ramin_address() + 0x0208);

        // TODO(Yusuke Suzuki): remove it
        write64(&pramin, ramin_address() + 0x0200, page_directory_phys);

        write64(ramin_.get(), 0x0200, page_directory_phys);
        CROSS_LOG("virt 0x%" PRIX64 " phys 0x%" PRIX64 "\n", page_directory_virt, page_directory_phys);

        // fctx
        const uint64_t fctx_virt = read64(&pramin, ramin_address() + 0x08);
        const uint64_t fctx_phys = ctx->get_phys_address(fctx_virt);

        // TODO(Yusuke Suzuki): remove it
        write64(&pramin, ramin_address() + 0x08, fctx_phys);

        write64(ramin_.get(), 0x08, fctx_phys);

        // mpeg ctx
        const uint64_t mpeg_ctx_limit_virt = pramin.read32(ramin_address() + 0x60 + 0x04);
        const uint64_t mpeg_ctx_limit_phys = ctx->get_phys_address(mpeg_ctx_limit_virt);

        // TODO(Yusuke Suzuki): remove it
        pramin.write32(ramin_address() + 0x60 + 0x04, mpeg_ctx_limit_phys);

        ramin_->write32(0x60 + 0x04, mpeg_ctx_limit_phys);

        const uint64_t mpeg_ctx_virt = pramin.read32(ramin_address() + 0x60 + 0x08);
        const uint64_t mpeg_ctx_phys = ctx->get_phys_address(mpeg_ctx_virt);

        // TODO(Yusuke Suzuki): remove it
        pramin.write32(ramin_address() + 0x60 + 0x08, mpeg_ctx_phys);

        ramin_->write32(0x60 + 0x08, mpeg_ctx_phys);
    }
    table()->refresh(ctx, page_directory_phys, page_directory_size);
    ctx->barrier()->map(ramin_address());
}

uint64_t channel::refresh(context* ctx, uint64_t addr) {
    CROSS_LOG("mapping 0x%" PRIX64 "\n", addr);
    if (enabled()) {
        if (addr == ramin_address()) {
            // same channel ramin
            return ramin_->address();
        }
        detach(ctx, addr);
    }
    enabled_ = true;
    ramin_address_ = addr;
    attach(ctx, addr);
    return ramin_->address();
}

}  // namespace cross
/* vim: set sw=4 ts=4 et tw=80 : */
